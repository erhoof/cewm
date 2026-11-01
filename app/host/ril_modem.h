#pragma once

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

namespace cewm {
// Stateful AT transport for the devemu RIL. Callbacks accept requests, not call connection or SMS delivery.
class RilModem {
public:
    enum class CallState { Idle = 0, Calling = 1, Connecting = 2, Active = 3, Held = 4, Incoming = 6 };
    std::function<bool(const std::string&)> dial;
    std::function<bool()> hangup, answer;
    std::function<bool(const std::string&)> control;
    std::function<int()> muted;

    // Called on the emulator thread when the host reports a real state change.
    std::string CallEvent(CallState state, const std::string& number = "", int direction = -1) {
        if (!number.empty() || state == CallState::Incoming || state == CallState::Calling) number_ = number;
        if (state == CallState::Incoming) incoming_ = true;
        if (state == CallState::Calling) incoming_ = false;
        if (direction >= 0) incoming_ = direction != 0;
        call_ = state;
        return monitoring_ ? CallReport() : "";
    }

    // The traced ROM selects direct +CMT delivery. The host must retain messages when this returns empty.
    std::string SmsEvent(const std::string& pdu) const {
        if (!direct_sms_ || pdu.size() < 4 || pdu.size() > 512 || pdu.size() % 2) return "";
        for (unsigned char c : pdu) if (!std::isxdigit(c)) return "";
        const size_t header = 1 + std::stoul(pdu.substr(0, 2), nullptr, 16);
        if (header >= pdu.size() / 2 || (std::stoul(pdu.substr(header * 2, 2), nullptr, 16) & 3) != 0) return "";
        return "\r\n+CMT: ," + std::to_string(pdu.size() / 2 - header) + "\r\n" + pdu + "\r\n";
    }

    template<class LegacyAnswer>
    std::string Feed(unsigned char ch, bool& numeric, bool& no_echo, LegacyAnswer legacy) {
        if (payload_) {
            if (ch == 0x1b) { ClearInput(); return Final(true, numeric); }
            if (ch == 0x1a) {
                const bool valid = !overflow_ && ValidSubmitEnvelope();
                if (getenv("CEWM_RIL_TRACE")) fprintf(stderr, "[RIL] SMS-SUBMIT bytes=%zu tpdu=%zu envelope_valid=%d\n", buffer_.size() / 2, tpdu_, valid);
                ClearInput();
                // shortcut: SMS host submission is not wired yet; add async +CMGS completion with the Android backend.
                return "\r\n+CMS ERROR: 500\r\n";
            }
            if (ch != '\r' && ch != '\n') Append(ch, 512);
            return "";
        }
        if (ch == '\n') return "";
        if (ch != '\r') { Append(ch, 512); return ""; }
        std::string line = buffer_;
        const bool too_long = overflow_;
        ClearInput();
        if (getenv("CEWM_RIL_TRACE")) fprintf(stderr, "[RIL] AT %s%s\n", line.c_str(), too_long ? " [overflow]" : "");
        if (too_long) return Final(false, numeric);
        std::string upper = line;
        for (char& c : upper) c = (char)std::toupper((unsigned char)c);
        const std::string echo = no_echo ? "" : line + "\r";
        if (upper.rfind("ATD", 0) == 0) {
            std::string number = line.substr(3);
            bool voice = !number.empty() && number.back() == ';';
            if (voice) number.pop_back();
            bool valid = voice && !number.empty() && number != "+" && number.size() <= 32;
            for (size_t i = 0; i < number.size(); ++i)
                valid &= (number[i] >= '0' && number[i] <= '9') || (i == 0 && number[i] == '+');
            const bool accepted = valid && call_ == CallState::Idle && dial && dial(number);
            return echo + Final(accepted, numeric) + (accepted ? CallEvent(CallState::Calling, number) : "");
        }
        if (upper == "ATH" || upper == "ATH0" || upper == "AT+CHUP")
            return echo + Final(hangup && hangup(), numeric);
        if (upper == "AT+CMUT?") {
            const int value = muted ? muted() : -1;
            return echo + (value == 0 || value == 1 ? "\r\n+CMUT: " + std::to_string(value) + "\r\n" + Final(true, numeric) : Final(false, numeric));
        }
        if (upper.rfind("AT+CMUT=", 0) == 0) return echo + Final(control && control(upper), numeric);
        if (upper == "ATA") return echo + Final(answer && answer(), numeric);
        if (upper == "AT+CHLD" || upper.rfind("AT+CHLD=", 0) == 0 || upper.rfind("AT+VTS=", 0) == 0)
            return echo + Final(control && control(upper), numeric);
        if (upper == "AT*ECAM=0" || upper == "AT*ECAM=1") {
            monitoring_ = upper.back() == '1';
            return echo + Final(true, numeric) + (monitoring_ ? CallReport() : "");
        }
        if (upper == "AT*ECAM?") return echo + "\r\n*ECAM: " + (monitoring_ ? "1" : "0") + "\r\n" + Final(true, numeric);
        if (upper == "AT*ECAM=?") return echo + "\r\n*ECAM: (0-1)\r\n" + Final(true, numeric);
        if (upper == "AT+CLCC") {
            std::string info;
            if (call_ != CallState::Idle) {
                int state = call_ == CallState::Active ? 0 : call_ == CallState::Held ? 1 : call_ == CallState::Calling ? 2 : call_ == CallState::Connecting ? 3 : 4;
                info = "\r\n+CLCC: 1," + std::to_string(incoming_ ? 1 : 0) + "," + std::to_string(state) + ",0,0" + NumberField() + "\r\n";
            }
            return echo + info + Final(true, numeric);
        }
        if (upper.rfind("AT+CMGS=", 0) == 0) {
            const std::string count = upper.substr(8);
            size_t n = 0;
            bool valid = !count.empty();
            for (char c : count) {
                if (c < '0' || c > '9' || n > 255) { valid = false; break; }
                n = n * 10 + c - '0';
            }
            if (!valid || !n || n > 255) return echo + "\r\n+CMS ERROR: 304\r\n";
            payload_ = true; tpdu_ = n;
            return echo + "\r\n> ";
        }
        if (upper == "AT+CMGF?") return echo + "\r\n+CMGF: 0\r\n" + Final(true, numeric);
        if (upper == "AT+CMGF=?") return echo + "\r\n+CMGF: (0)\r\n" + Final(true, numeric);
        if (upper.rfind("AT+CMGF=", 0) == 0) return echo + Final(upper == "AT+CMGF=0", numeric);
        if (upper == "AT+CNMI=2,2,0,1,0" || upper == "AT+CNMI=0,0,0,0,0") {
            direct_sms_ = upper == "AT+CNMI=2,2,0,1,0";
            return echo + Final(true, numeric);
        }
        if (upper == "AT+CNMI?") return echo + "\r\n+CNMI: " + (direct_sms_ ? "2,2,0,1,0" : "0,0,0,0,0") + "\r\n" + Final(true, numeric);
        if (upper.rfind("AT+CNMI=", 0) == 0) return echo + Final(false, numeric);
        if (upper.rfind("AT+CMSS=", 0) == 0 || upper.rfind("AT+CMGW=", 0) == 0)
            return echo + "\r\n+CMS ERROR: 303\r\n";
        return legacy(line, numeric, no_echo);
    }

    // Bit 2 distinguishes a saved opt-out from legacy snapshots which lost subscriptions.
    unsigned Session() const { return 4u | (monitoring_ ? 1u : 0u) | (direct_sms_ ? 2u : 0u); }
    void RestoreSession(unsigned flags, bool active_link = false) {
        Reset();
        monitoring_ = (flags & 1) || (active_link && flags == 0);
        direct_sms_ = flags & 2;
    }
    void Reset() { ClearInput(); monitoring_ = incoming_ = direct_sms_ = false; call_ = CallState::Idle; number_.clear(); }

private:
    std::string NumberField() const {
        // Only dialable numbers may be interpolated into the AT stream.
        if (number_.empty() || number_.size() > 32) return "";
        for (size_t i = 0; i < number_.size(); ++i)
            if (!(number_[i] >= '0' && number_[i] <= '9') && !(i == 0 && number_[i] == '+')) return "";
        return ",\"" + number_ + "\"," + (number_[0] == '+' ? "145" : "129");
    }
    std::string CallReport() const {
        return "\r\n*ECAV: 1," + std::to_string((int)call_) + ",1,," + (call_ == CallState::Idle ? "16" : "") + NumberField() + "\r\n";
    }
    void ClearInput() { buffer_.clear(); payload_ = overflow_ = false; tpdu_ = 0; }
    static std::string Final(bool ok, bool numeric) {
        return numeric ? (ok ? "0\r" : "4\r") : (ok ? "\r\nOK\r\n" : "\r\nERROR\r\n");
    }
    void Append(unsigned char ch, size_t limit) {
        if (buffer_.size() < limit) buffer_ += (char)ch;
        else overflow_ = true;
    }
    bool ValidSubmitEnvelope() const {
        if (buffer_.size() < 4 || buffer_.size() % 2) return false;
        for (unsigned char c : buffer_) if (!std::isxdigit(c)) return false;
        const size_t smsc = std::stoul(buffer_.substr(0, 2), nullptr, 16);
        if (buffer_.size() / 2 != 1 + smsc + tpdu_) return false;
        const size_t first = 2 * (1 + smsc);
        return (std::stoul(buffer_.substr(first, 2), nullptr, 16) & 3) == 1;
    }
    std::string buffer_;
    bool payload_ = false, overflow_ = false;
    size_t tpdu_ = 0;
    // shortcut: one voice call; extend the IDs and direction tracking before supporting call waiting.
    CallState call_ = CallState::Idle;
    bool monitoring_ = false, incoming_ = false, direct_sms_ = false;
    std::string number_;
};
} // namespace cewm
