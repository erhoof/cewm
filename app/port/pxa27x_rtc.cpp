// POSIX port: P525 wristwatch alarm registers and interrupt delivery.
#include "peripherals/peripheral_base.h"

#include "boards/board_context.h"
#include "socs/pxa27x/pxa270_id.h"
#include "socs/pxa27x/pxa27x_clock_manager.h"
#include "socs/pxa27x/pxa27x_power_manager.h"
#include "core/cerf_emulator.h"
#include "core/fatal.h"
#include "host/guest_deep_sleep.h"
#include "peripherals/peripheral_dispatcher.h"
#include "state/state_stream.h"
#include "socs/guest_cpu_reset.h"
#include "socs/intel_rtc_counter.h"
#include "socs/irq_controller.h"
#include "socs/pxa2xx/pxa2xx_intc.h"

#include <chrono>
#include <cstdint>
#include <algorithm>
#include <ctime>

namespace {

/* Intel PXA27x Developer's Manual 280000-001 Table 25-2 (page 25-5): "IP[31]
   Real-time clock RTC equals Alarm register", "IP[30] One Hz clock TIC
   occurred". */
constexpr uint32_t kIntcAlarmBit = 1u << 31;
constexpr uint32_t kIntcHzBit    = 1u << 30;

/* Table 21-6 (page 21-16): "31 R/W LCK", "25:16 R/W DEL", "15:0 R/W CK_DIV";
   §21.5.1: "The reset value of this register (0x0000_7FFF)". */
constexpr uint32_t kRttrLck   = 0x80000000u;
constexpr uint32_t kRttrReset = 0x00007FFFu;

/* §3.8.2.3 (page 3-99): OOK "switches the clock source to the RTC and power
   manager from the 13-MHz processor oscillator divided by 400 to the
   timekeeping oscillator." */
constexpr uint64_t kOscCrystalHz = 32768u;
constexpr uint64_t kOscDividedHz = 13000000u / 400u;

/* Table 21-7 (pages 21-17..21-19): count and alarm enables other than HZE and
   ALE - PICE 15, PIALE 14, SWCE 12, SWALE2 11, SWALE1 9, RDALE2 7, RDALE1 5. */
constexpr uint32_t kRtsrOtherEnables = 0x0000DA00u;
constexpr uint32_t kRtsrLow          = 0x0000000Fu;

/* Table 21-14 / 21-15 bit fields. */
constexpr uint32_t kRdcrMask = 0x007FFFFFu;
constexpr uint32_t kRycrMask = 0x001FFFFFu;

constexpr int64_t kSecPerDay = 86400;

constexpr uint32_t kRycrFromShadow = 0x80000000u;

constexpr IntelRtcCounter::Traits kTraits = {
    "pxa27x", kRttrLck | 0x03FFFFFFu, kRttrLck, true, true, true,
};

int64_t FloorDiv(int64_t a, int64_t b) {
    const int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

uint32_t WomOfDay(uint32_t dom) { return (dom + 6u) / 7u; }

std::chrono::year_month_day DateOf(uint32_t rycr) {
    using namespace std::chrono;
    return year_month_day{year{static_cast<int>((rycr >> 9) & 0xFFFu)},
                          month{(rycr >> 5) & 0xFu}, day{rycr & 0x1Fu}};
}

/* Table 21-3 (page 21-7): Day of week (DOW) valid "1 to 7", invalid "0". */
bool WristwatchDataValid(uint32_t rdcr, uint32_t rycr) {
    return DateOf(rycr).ok() && ((rdcr >> 12) & 0x1Fu) <= 23u && ((rdcr >> 6) & 0x3Fu) <= 59u &&
           (rdcr & 0x3Fu) <= 59u && ((rdcr >> 17) & 0x7u) != 0u;
}

class Pxa27xRtc : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Pxa270;
    }

    void OnReady() override {
        host_calendar_ = emu_.Get<BoardContext>().GetBoardFamilyId() == "asus_p525";
        rtc_.Attach(kOscDividedHz, 1u);
        rtc_.SetExtLand([this](uint32_t rdcr, uint32_t rycr) { LandWristwatch(rdcr, rycr); });
        rtc_.ResetRttr(kRttrReset);
        ResetWristwatch();
        auto& clocks = emu_.Get<Pxa27xClockManager>();
        auto& cycle_clock = emu_.Get<GuestCycleClock>();
        ww_event_ = cycle_clock.Add([this] { RearmWristwatch(); });
        cycle_clock.RegisterRateListener([this] { RearmWristwatch(); });
        clocks.RegisterOscillatorListener([this] { ApplyOscillator(); });
        ApplyOscillator();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            OnResetLine(kind);
        });
        auto& sleep = emu_.Get<GuestDeepSleep>();
        /* §21.5.2 (page 21-17): in sleep "the alarm-detect bit in the RTSR is updated if
           an RTC alarm is detected and the corresponding alarm-enable bit in the RTSR is set". */
        sleep.RegisterSleepEntryListener([this] {
            match_mark_ = rtc_.MatchEvents();
            ww_mark_    = ww_matches_;
        });
        /* §21.4.6.2 (page 21-15): with PWER[WERTC] set, wake-up on "a match
           between RCNR and RTAR ... regardless of the state of the RTSR[ALE]
           bit", and on wristwatch matches regardless of RDALE1/2. */
        auto rtc_woke = [this] {
            return emu_.Get<Pxa27xPowerManager>().RtcWakeEnabled() &&
                   (rtc_.MatchEvents() != match_mark_ || ww_matches_ != ww_mark_);
        };
        sleep.RegisterParkClock([this, rtc_woke] {
            if (rtc_woke()) emu_.Get<Pxa27xPowerManager>().LatchRtcWakeEdge();
        });
        sleep.RegisterParkWakeSource(rtc_woke);
        sleep.RegisterParkWakeDue([this] {
            if (!emu_.Get<Pxa27xPowerManager>().RtcWakeEnabled()) {
                return GuestDeepSleep::kNoParkWake;
            }
            const int64_t match = rtc_.SleptNsAtEdge(rtc_.EdgesToMatch());
            int64_t next = match;
            for (unsigned i = 0; i < 2; ++i) {
                const uint64_t edges = NextWristwatchMatch(i);
                if (edges == 0u) continue;
                const int64_t due = rtc_.SleptNsAtEdge(edges);
                if (due < next) next = due;
            }
            return next;
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
        rtc_.Rearm();
        RearmWristwatch();
    }

    uint32_t MmioBase() const override { return 0x40900000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override {
        SyncHostCalendar();
        PushLevel();
        rtc_.Rearm();
        RearmWristwatch();
    }

private:
    void SyncHostCalendar() {
        if (!host_calendar_) return;
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        using namespace std::chrono;
        const year_month_day date{year{local.tm_year + 1900},
                                  month{static_cast<unsigned>(local.tm_mon + 1)},
                                  day{static_cast<unsigned>(local.tm_mday)}};
        const auto seconds_now = sys_days{date} + hours{local.tm_hour} +
                                 minutes{local.tm_min} + seconds{local.tm_sec};
        const int64_t host_seconds = seconds_now.time_since_epoch().count();
        const int64_t previous = WristwatchNow();
        const int64_t evaluated = ww_eval_;
        SetWristwatch(host_seconds);
        // Keep normal alarm evaluation across synchronization; a restored old
        // date must not replay years of alarms when it jumps to host time.
        if (host_seconds >= previous - 60 && host_seconds <= previous + 60)
            ww_eval_ = evaluated;
        SetDayFields(local.tm_wday + 1u, WomOfDay(local.tm_mday), local.tm_mday);
    }

    void ApplyOscillator() {
        if (emu_.Get<Pxa27xClockManager>().OscillatorOk()) {
            rtc_.SetOscRate(kOscCrystalHz, 1u);
        } else {
            rtc_.SetOscRate(kOscDividedHz, 1u);
        }
    }

    void PushLevel() {
        const uint32_t rtsr = rtc_.Rtsr();
        uint32_t level = 0;
        if ((rtsr & IntelRtcCounter::kRtsrAl) && (rtsr & IntelRtcCounter::kRtsrAle))
            level |= kIntcAlarmBit;
        if ((rtsr & IntelRtcCounter::kRtsrHz) && (rtsr & IntelRtcCounter::kRtsrHze))
            level |= kIntcHzBit;
        if ((ww_rtsr_ & (ww_rtsr_ >> 1) & 0x50u) != 0u) level |= kIntcAlarmBit;
        static_cast<Pxa2xxIntc&>(emu_.Get<IrqController>())
            .SetSourceLevel(kIntcAlarmBit | kIntcHzBit, level);
    }

    void OnStatus() {
        CountWristwatchMatches();
        PushLevel();
    }

    /* Figure 21-1 (page 21-3): the trimmer's 1-Hz clock feeds both the timer
       and the wristwatch. */
    int64_t WristwatchNow() const {
        return ww_base_ + static_cast<int64_t>(rtc_.HzEdges() - ww_mark_edges_);
    }

    bool AlarmDayMatches(unsigned i, int64_t day) const;
    uint64_t NextWristwatchMatch(unsigned i) const;
    void RearmWristwatch() {
        rtc_.Advance();
        CountWristwatchMatches();
        PushLevel();
        if (!ww_event_) return;
        auto& clock = emu_.Get<GuestCycleClock>();
        const int64_t due = rtc_.SleptNsAtEdge(1u);
        if (due == GuestDeepSleep::kNoParkWake) { clock.Disarm(ww_event_); return; }
        const int64_t delay = due - emu_.Get<GuestDeepSleep>().SleptNs();
        clock.Arm(ww_event_, clock.Cycles() + std::max<uint64_t>(1u, clock.NsToCycles(std::max<int64_t>(1, delay))));
    }

    /* Intel PXA27x manual §21.4.2.3: zero date fields are wildcards;
       hour/minute/second fields always participate in the comparison. */
    void CountWristwatchMatches() {
        const int64_t now = WristwatchNow();
        if (now > ww_eval_) {
            for (unsigned i = 0; i < 2; ++i) {
                const uint32_t a = rdar_[i];
                const unsigned hr = (a >> 12) & 31u, min = (a >> 6) & 63u, sec = a & 63u;
                if (hr > 23u || min > 59u || sec > 59u) continue;
                const int64_t tod = hr * 3600 + min * 60 + sec;
                for (int64_t day = FloorDiv(ww_eval_, kSecPerDay); day <= FloorDiv(now, kSecPerDay); ++day) {
                    const int64_t candidate = day * kSecPerDay + tod;
                    if (candidate <= ww_eval_ || candidate > now || !AlarmDayMatches(i, day)) continue;
                    ++ww_matches_;
                    const uint32_t status = 0x10u << (i * 2u);
                    if (ww_rtsr_ & (status << 1u)) ww_rtsr_ |= status;
                }
            }
        }
        ww_eval_ = now;
    }

    void SetWristwatch(int64_t secs) {
        ww_base_       = secs;
        ww_mark_edges_ = rtc_.HzEdges();
        ww_eval_       = secs;
    }

    /* Table 21-14 (page 21-24) Reset: WOM 1, DOW 7, 00:00:00; Table 21-15
       (page 21-25) Reset: YEAR 2000, MONTH 1, DOM 1. */
    void ResetWristwatch() {
        rtc_.Advance();
        using namespace std::chrono;
        SetWristwatch(sys_seconds{sys_days{January / 1 / 2000}}.time_since_epoch().count());
        SetDayFields(7u, 1u, 1u);
        rycr_shadow_  = 0u;
        rycr_pending_ = false;
        for (unsigned i = 0; i < 2; ++i) {
            rdar_[i] = ryar_[i] = ryar_shadow_[i] = 0u;
            ryar_pending_[i] = false;
        }
        ww_rtsr_ = 0u;
        SyncHostCalendar();
    }

    /* §21.4.2 (page 21-6): "The counters subsection counts the current time and year";
       §21.4.2.3.2 (page 21-8): "Any attempt to write invalid or incorrect data to the
       counter registers results in unpredictable behavior." */
    void SetDayFields(uint32_t dow, uint32_t wom, uint32_t dom) {
        dow_        = dow;
        dow_day_    = FloorDiv(ww_base_, kSecPerDay);
        wom_held_   = wom;
        wom_tracks_ = wom == WomOfDay(dom);
    }

    void OnResetLine(ResetLineKind kind) {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        if (kind == ResetLineKind::Other) return;
        rtc_.ResetCounter();
        if (kind == ResetLineKind::Rtc) rtc_.ResetRttr(kRttrReset);
        ResetWristwatch();
        RearmWristwatch();
    }

    uint32_t ReadRdcr() const;
    uint32_t ReadRdcrAt(int64_t seconds) const;
    uint32_t ReadRycr() const;
    void     WriteRdcr(uint32_t rdcr);
    void     LandWristwatch(uint32_t rdcr, uint32_t rycr);
    void     WriteRtsr(uint32_t value);

    IntelRtcCounter rtc_{emu_, kTraits, [this] { OnStatus(); }};
    bool            host_calendar_ = false;
    uint64_t        match_mark_ = 0;
    int64_t         ww_base_       = 0;
    uint64_t        ww_mark_edges_ = 0;
    int64_t         ww_eval_       = 0;
    uint64_t        ww_matches_    = 0;
    uint64_t        ww_mark_       = 0;
    uint32_t        rycr_shadow_   = 0;
    bool            rycr_pending_  = false;
    uint32_t        dow_           = 7;
    int64_t         dow_day_       = 0;
    uint32_t        wom_held_      = 1;
    bool            wom_tracks_    = true;
    uint32_t        rdar_[2] = {}, ryar_[2] = {}, ryar_shadow_[2] = {};
    bool            ryar_pending_[2] = {};
    uint32_t        ww_rtsr_ = 0u;
    GuestCycleClock::Event* ww_event_ = nullptr;
};

bool Pxa27xRtc::AlarmDayMatches(unsigned i, int64_t day) const {
    using namespace std::chrono;
    const year_month_day date{sys_days{days{day}}};
    const uint32_t year = (ryar_[i] >> 9) & 0xFFFu, month = (ryar_[i] >> 5) & 15u, dom = ryar_[i] & 31u;
    if ((year && year != (uint32_t)int(date.year())) || (month && month != unsigned(date.month()))) return false;
    const year_month_day target{date.year(), date.month(), std::chrono::day{dom}};
    if (dom && target.ok()) return dom == unsigned(date.day());
    const uint32_t rdcr = ReadRdcrAt(day * kSecPerDay);
    const unsigned dow = (rdar_[i] >> 17) & 7u, wom = (rdar_[i] >> 20) & 7u;
    const unsigned live_wom = (rdcr >> 20) & 7u;
    if (dow && dow != ((rdcr >> 17) & 7u)) return false;
    if (wom == 6u) return live_wom == 1u || live_wom == 3u;
    if (wom == 7u) return live_wom == 2u || live_wom == 4u;
    return wom == 0u || wom == live_wom;
}

uint64_t Pxa27xRtc::NextWristwatchMatch(unsigned i) const {
    const unsigned hr = (rdar_[i] >> 12) & 31u, min = (rdar_[i] >> 6) & 63u, sec = rdar_[i] & 63u;
    if (hr > 23u || min > 59u || sec > 59u) return 0u;
    const int64_t now = WristwatchNow(), tod = hr * 3600 + min * 60 + sec;
    const int64_t last = std::chrono::sys_days{std::chrono::year{4095}/12/31}.time_since_epoch().count();
    for (int64_t day = FloorDiv(now, kSecPerDay); day <= last; ++day) {
        const int64_t candidate = day * kSecPerDay + tod;
        if (candidate > now && AlarmDayMatches(i, day)) return static_cast<uint64_t>(candidate - now);
    }
    return 0u;
}

uint32_t Pxa27xRtc::ReadRdcr() const {
    return ReadRdcrAt(WristwatchNow());
}

uint32_t Pxa27xRtc::ReadRdcrAt(int64_t at) const {
    using namespace std::chrono;
    const sys_seconds now{seconds{at}};
    const auto days = floor<std::chrono::days>(now);
    const hh_mm_ss hms{now - days};
    /* Table 21-14 (page 21-24): "19:17 R/W DOW Day of Week-1 (Sunday) through 7
       (Saturday)", "22:20 R/W WOM Week of Month-1 (first week) through 5". */
    const int64_t  elapsed = (days.time_since_epoch().count() - dow_day_) % 7;
    const uint32_t dow = static_cast<uint32_t>((static_cast<int64_t>(dow_) - 1 + elapsed + 7) % 7) + 1u;
    const uint32_t dom = unsigned{year_month_day{days}.day()};
    const uint32_t wom = wom_tracks_ ? WomOfDay(dom) : wom_held_;
    return (wom << 20) | (dow << 17) |
           (static_cast<uint32_t>(hms.hours().count())   << 12) |
           (static_cast<uint32_t>(hms.minutes().count()) <<  6) |
            static_cast<uint32_t>(hms.seconds().count());
}

uint32_t Pxa27xRtc::ReadRycr() const {
    using namespace std::chrono;
    const year_month_day ymd{floor<days>(sys_seconds{seconds{WristwatchNow()}})};
    return (static_cast<uint32_t>(int{ymd.year()}) << 9) |
           (static_cast<uint32_t>(unsigned{ymd.month()}) << 5) |
            static_cast<uint32_t>(unsigned{ymd.day()});
}

void Pxa27xRtc::WriteRdcr(uint32_t rdcr) {
    rtc_.Advance();
    const uint32_t rycr = rycr_pending_ ? rycr_shadow_ : ReadRycr();
    if (!WristwatchDataValid(rdcr, rycr)) {
        emu_.Get<Fatal>().Die("Pxa27xRtc: wristwatch write with invalid data RDCR 0x%08X RYCR 0x%08X",
                              rdcr, rycr);
    }
    const uint32_t shadow = rycr_pending_ ? (kRycrFromShadow | rycr_shadow_) : 0u;
    rycr_pending_ = false;
    rtc_.WriteExt(rdcr, shadow);
}

/* §21.4.2.3.1 (page 21-8): "Write the RDCR with new data-the RYCR (with current data) and
   the RDCR (with new data) is written." */
void Pxa27xRtc::LandWristwatch(uint32_t rdcr, uint32_t shadow) {
    using namespace std::chrono;
    const uint32_t       rycr = (shadow & kRycrFromShadow) != 0u ? (shadow & kRycrMask) : ReadRycr();
    const year_month_day ymd  = DateOf(rycr);
    const sys_seconds t = sys_seconds{sys_days{ymd}} +
                          hours{static_cast<int>((rdcr >> 12) & 0x1Fu)} +
                          minutes{static_cast<int>((rdcr >> 6) & 0x3Fu)} +
                          seconds{static_cast<int>(rdcr & 0x3Fu)};
    CountWristwatchMatches();
    SetWristwatch(t.time_since_epoch().count());
    SetDayFields((rdcr >> 17) & 0x7u, (rdcr >> 20) & 0x7u, unsigned{ymd.day()});
}

/* Table 21-7: AL / HZ clear by writing ones, ALE / HZE are read/write. */
void Pxa27xRtc::WriteRtsr(uint32_t value) {
    if ((value & kRtsrOtherEnables) != 0u) {
        emu_.Get<Fatal>().Die("Pxa27xRtc: RTSR write 0x%08X enables a stopwatch, periodic or "
                              "alarm, which is not modelled", value);
    }
    rtc_.Advance();
    CountWristwatchMatches();
    ww_rtsr_ = (ww_rtsr_ & 0x50u & ~value) | (value & 0xA0u);
    rtc_.WriteRtsr(value & kRtsrLow);
    RearmWristwatch();
}

uint32_t Pxa27xRtc::ReadWord(uint32_t addr) {
    SyncHostCalendar();
    rtc_.Rearm();
    switch (addr - MmioBase()) {
        case 0x00: return rtc_.Rcnr();
        case 0x04: return rtc_.Rtar();
        case 0x08: CountWristwatchMatches(); PushLevel(); return rtc_.Rtsr() | ww_rtsr_;
        case 0x0C: return rtc_.Rttr();
        case 0x10: return ReadRdcr() & kRdcrMask;
        case 0x14: return ReadRycr() & kRycrMask;
        case 0x18: return rdar_[0];
        case 0x1C: return ryar_[0];
        case 0x20: return rdar_[1];
        case 0x24: return ryar_[1];
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Pxa27xRtc::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off == 0x18u || off == 0x1Cu || off == 0x20u || off == 0x24u) {
        rtc_.Advance();
        CountWristwatchMatches();
        const unsigned i = (off - 0x18u) / 8u;
        if (off & 4u) {
            ryar_shadow_[i] = value & kRycrMask;
            ryar_pending_[i] = true;
        } else {
            rdar_[i] = value & kRdcrMask;
            if (ryar_pending_[i]) ryar_[i] = ryar_shadow_[i];
            ryar_pending_[i] = false;
        }
        RearmWristwatch();
        return;
    }
    switch (addr - MmioBase()) {
        case 0x00: rtc_.WriteRcnr(value); return;
        case 0x04: rtc_.WriteRtar(value); return;
        case 0x08: WriteRtsr(value);      return;
        case 0x0C: rtc_.WriteRttr(value);         return;
        case 0x10: if (!host_calendar_) WriteRdcr(value & kRdcrMask); return;
        /* §21.4.2.3.1 (page 21-8): "When the write for RYCR is executed, the
           new data for RYCR is first written into an internal register." */
        case 0x14:
            if (host_calendar_) return;
            rycr_shadow_  = value & kRycrMask;
            rycr_pending_ = true;
            return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Pxa27xRtc::SaveState(StateWriter& w) {
    rtc_.Save(w);
    w.Write("ww_live", WristwatchNow());
    w.Write("rycr_shadow", rycr_shadow_);
    w.Write<uint8_t>("rycr_pending", rycr_pending_ ? 1u : 0u);
    w.Write("ww_dow", (ReadRdcr() >> 17) & 0x7u);
    w.Write("ww_wom_held", wom_held_);
    w.Write<uint8_t>("ww_wom_tracks", wom_tracks_ ? 1u : 0u);
    w.Write("rdar", rdar_);
    w.Write("ryar", ryar_);
    w.Write("ryar_alarm_shadow", ryar_shadow_);
    for (unsigned i = 0; i < 2; ++i) w.Write<uint8_t>(i ? "ryar_pending_2" : "ryar_pending_1", ryar_pending_[i] ? 1u : 0u);
    w.Write("ww_rtsr", ww_rtsr_);
}

void Pxa27xRtc::RestoreState(StateReader& r) {
    rtc_.Restore(r);
    int64_t  ww_live = 0;
    uint8_t  pending = 0, tracks = 0;
    uint32_t dow = 0;
    r.Read("ww_live", ww_live);
    r.Read("rycr_shadow", rycr_shadow_);
    r.Read("rycr_pending", pending);
    r.Read("ww_dow", dow);
    r.Read("ww_wom_held", wom_held_);
    r.Read("ww_wom_tracks", tracks);
    if (pending > 1u || tracks > 1u || dow == 0u || dow > 7u || wom_held_ > 7u) {
        r.Reject("Pxa27xRtc: restored RYCR pending %u, DOW %u, WOM %u or WOM tracking %u out of "
                 "range", pending, dow, wom_held_, tracks);
    }
    rycr_pending_ = pending != 0u;
    wom_tracks_   = tracks != 0u;
    SetWristwatch(ww_live);
    dow_     = dow;
    dow_day_ = FloorDiv(ww_live, kSecPerDay);
    // Earlier snapshots predate wristwatch alarms and retain their reset values.
    if (r.Remaining() != 0u) {
        r.Read("rdar", rdar_);
        r.Read("ryar", ryar_);
        r.Read("ryar_alarm_shadow", ryar_shadow_);
        for (unsigned i = 0; i < 2; ++i) {
            uint8_t pending_alarm = 0u;
            r.Read(i ? "ryar_pending_2" : "ryar_pending_1", pending_alarm);
            if (pending_alarm > 1u) r.Reject("Pxa27xRtc: invalid alarm shadow flag");
            ryar_pending_[i] = pending_alarm != 0u;
            if ((rdar_[i] & ~kRdcrMask) || (ryar_[i] & ~kRycrMask) || (ryar_shadow_[i] & ~kRycrMask)) r.Reject("Pxa27xRtc: invalid alarm bits");
        }
        r.Read("ww_rtsr", ww_rtsr_);
        if (ww_rtsr_ & ~0xF0u) r.Reject("Pxa27xRtc: invalid alarm status");
    }
}

}  // namespace

REGISTER_SERVICE(Pxa27xRtc);
