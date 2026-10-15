/* The device rail scrolls sideways, and the tiles inside it already own their
   clicks (slideshow.js steps the screenshots), so the row takes the wheel and
   pointer drags instead of a swipe. The grip along the bottom edge is the row's
   scrollbar: it reports the position and drags like the real one. */
(function () {
  var DRAG_SLOP = 5;
  var GRIP_MIN = 32;

  var observers = [];

  function setup(rail) {
    if (rail.dataset.cerfRail) return;

    var track = rail.querySelector('.cerf-rail-track');
    if (!track) return;

    rail.dataset.cerfRail = '1';
    rail.classList.add('cerf-rail--js');

    var bar = document.createElement('div');
    bar.className = 'cerf-rail-bar';
    var grip = document.createElement('div');
    grip.className = 'cerf-rail-grip';
    bar.appendChild(grip);
    rail.appendChild(bar);

    function range() {
      return track.scrollWidth - track.clientWidth;
    }

    var queued = false;
    function sync() {
      queued = false;
      var max = range();
      if (max < 1) {
        bar.hidden = true;
        return;
      }
      bar.hidden = false;

      var span = bar.clientWidth;
      var width = Math.max(span * (track.clientWidth / track.scrollWidth), GRIP_MIN);
      grip.style.width = width + 'px';
      grip.style.transform =
        'translateX(' + (track.scrollLeft / max) * (span - width) + 'px)';
    }

    function schedule() {
      if (queued) return;
      queued = true;
      requestAnimationFrame(sync);
    }

    track.addEventListener('scroll', schedule);

    if ('ResizeObserver' in window) {
      var observer = new ResizeObserver(schedule);
      observer.observe(track);
      observers.push(observer);
    } else {
      window.addEventListener('resize', schedule);
    }

    /* A wheel that the row cannot answer belongs to the page: hijacking it at
       either end traps the reader inside the row. */
    track.addEventListener('wheel', function (e) {
      if (!e.deltaY) return;

      var max = range();
      if (max < 1) return;
      if (e.deltaY < 0 && track.scrollLeft < 1) return;
      if (e.deltaY > 0 && track.scrollLeft > max - 1) return;

      var step = e.deltaY;
      if (e.deltaMode === 1) step *= 16;
      else if (e.deltaMode === 2) step *= track.clientWidth;

      e.preventDefault();
      track.scrollLeft += step;
    }, { passive: false });

    var swallow = false;

    /* Pointer capture retargets the click to the element that holds it, and the
       tiles own their clicks, so a press waits out the slop before it becomes a
       drag and takes the pointer. */
    function drag(el, scale, cls, slop) {
      var from = 0;
      var base = 0;
      var moved = 0;
      var armed = false;
      var live = false;

      function take(e) {
        live = true;
        el.setPointerCapture(e.pointerId);
        rail.classList.add(cls);
      }

      el.addEventListener('pointerdown', function (e) {
        if (e.pointerType === 'touch' || e.button !== 0) return;
        from = e.clientX;
        base = track.scrollLeft;
        moved = 0;
        armed = true;
        if (el === track) swallow = false;
        if (!slop) {
          e.preventDefault();
          take(e);
        }
      });

      el.addEventListener('pointermove', function (e) {
        if (!armed) return;

        var by = e.clientX - from;
        moved = Math.max(moved, Math.abs(by));
        if (!live) {
          if (moved <= slop) return;
          take(e);
        }
        track.scrollLeft = base + by * scale();
      });

      function end() {
        armed = false;
        if (!live) return;
        live = false;
        rail.classList.remove(cls);
        if (el === track) swallow = true;
      }

      el.addEventListener('pointerup', end);
      el.addEventListener('pointercancel', end);
    }

    drag(track, function () { return -1; }, 'cerf-rail--drag', DRAG_SLOP);
    drag(grip, function () {
      var span = bar.clientWidth - grip.clientWidth;
      return span > 0 ? range() / span : 0;
    }, 'cerf-rail--gripping', 0);

    /* The tile under the pointer would otherwise read the end of a drag as a
       click and step its screenshots. */
    track.addEventListener('click', function (e) {
      if (!swallow) return;
      swallow = false;
      e.preventDefault();
      e.stopPropagation();
    }, true);

    sync();
  }

  function arm() {
    observers.forEach(function (o) { o.disconnect(); });
    observers = [];
    document.querySelectorAll('.cerf-rail').forEach(setup);
  }

  if (typeof document$ !== 'undefined') {
    document$.subscribe(arm);
  } else {
    document.addEventListener('DOMContentLoaded', arm);
  }
})();
