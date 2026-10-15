/* Device tiles are slideshows over the screenshots in their folder. The slide
   list is emitted by hooks/render.py as data-cerf-slides on the tile's <img>.

   Each multi-shot tile gets: a click stage (left half = previous, right half =
   next), a dot per slide showing position, and the same staggered auto-advance
   the wall has always had. A manual click restarts the auto-advance timer so it
   does not immediately step off the slide the reader just chose.

   No transition on the swap: with a wall of tiles cycling at once, any fade
   reads as flicker across the page. The images are preloaded, so the swap is a
   single clean repaint. */
(function () {
  var HOLD_MS = 7000;

  var timers = [];

  function play(img) {
    if (img.dataset.cerfReady) return;

    var slides = [img.getAttribute('src')].concat(
      img.getAttribute('data-cerf-slides').split(' ').filter(Boolean));
    if (slides.length < 2) return;

    img.dataset.cerfReady = '1';
    slides.forEach(function (src) { new Image().src = src; });

    var stage = document.createElement('div');
    stage.className = 'cerf-slideshow';
    img.parentNode.insertBefore(stage, img);
    stage.appendChild(img);

    var dots = document.createElement('div');
    dots.className = 'cerf-dots';
    var dotEls = slides.map(function (_, n) {
      var dot = document.createElement('button');
      dot.type = 'button';
      dot.className = 'cerf-dot';
      dot.setAttribute('aria-label', 'Screenshot ' + (n + 1) + ' of ' + slides.length);
      dot.addEventListener('click', function (e) {
        e.stopPropagation();
        e.preventDefault();
        show(n);
        restart();
      });
      dots.appendChild(dot);
      return dot;
    });
    stage.appendChild(dots);

    var i = 0;
    function show(n) {
      i = (n + slides.length) % slides.length;
      img.src = slides[i];
      dotEls.forEach(function (dot, k) {
        dot.classList.toggle('cerf-dot--active', k === i);
      });
    }

    var interval = null;
    function restart() {
      clearInterval(interval);
      interval = setInterval(function () {
        if (document.hidden) return;
        show(i + 1);
      }, HOLD_MS);
      timers.push(interval);
    }

    stage.addEventListener('click', function (e) {
      e.stopPropagation();
      e.preventDefault();
      var rect = stage.getBoundingClientRect();
      var toLeft = (e.clientX - rect.left) < rect.width / 2;
      show(toLeft ? i - 1 : i + 1);
      restart();
    });

    show(0);

    // Spread the tiles across the cycle so the wall never swaps in unison.
    var offset = parseInt(img.getAttribute('data-cerf-offset'), 10) || 0;
    timers.push(setTimeout(restart, offset));
  }

  function stop() {
    timers.forEach(function (t) { clearTimeout(t); clearInterval(t); });
    timers = [];
  }

  function arm() {
    stop();
    if (matchMedia('(prefers-reduced-motion: reduce)').matches) return;
    document.querySelectorAll('img[data-cerf-slides]').forEach(play);
  }

  if (typeof document$ !== 'undefined') {
    document$.subscribe(arm);
  } else {
    document.addEventListener('DOMContentLoaded', arm);
  }
})();
