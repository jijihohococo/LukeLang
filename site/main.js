/* LukeLang — lukelang.org
   Scroll-driven motion only. Nothing here tracks or restyles the pointer. */

(function () {
  'use strict';

  var reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* ── Hero wordmark: split into animated characters ─────────────── */
  document.querySelectorAll('[data-split]').forEach(function (node) {
    var text = node.textContent.trim();
    node.textContent = '';
    text.split('').forEach(function (ch, i) {
      var span = document.createElement('span');
      // "Lang" picks up the golden half of the identity, as in the logo.
      span.className = i >= 4 ? 'ch ch--accent' : 'ch';
      span.textContent = ch;
      span.style.animationDelay = 220 + i * 55 + 'ms';
      node.appendChild(span);
    });
  });

  /* ── Reveal on enter ───────────────────────────────────────────── */
  var revealables = document.querySelectorAll('[data-reveal]');
  revealables.forEach(function (el) {
    if (el.dataset.delay) el.style.setProperty('--d', el.dataset.delay);
  });

  var reveal = new IntersectionObserver(function (entries) {
    entries.forEach(function (entry) {
      if (!entry.isIntersecting) return;
      entry.target.classList.add('is-in');
      reveal.unobserve(entry.target);
    });
  }, { rootMargin: '0px 0px -12% 0px', threshold: 0.15 });

  revealables.forEach(function (el) { reveal.observe(el); });

  /* Elements that style .is-in themselves, entering a little later. */
  var extras = new IntersectionObserver(function (entries) {
    entries.forEach(function (entry) {
      if (!entry.isIntersecting) return;
      entry.target.classList.add('is-in');
      extras.unobserve(entry.target);
    });
  }, { rootMargin: '0px 0px -18% 0px', threshold: 0.3 });
  document.querySelectorAll('.claims p, .creed li, .footer__mark img').forEach(function (el) {
    extras.observe(el);
  });

  /* ── Pipeline: draw the path, then walk the stages ─────────────── */
  var pipe = document.querySelector('[data-pipe]');
  if (pipe) {
    var pipeWatch = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        pipe.classList.add('is-live');
        pipeWatch.disconnect();
      });
    }, { threshold: 0.35 });
    pipeWatch.observe(pipe);
  }

  /* ── Count-up figures ──────────────────────────────────────────── */
  var counters = new IntersectionObserver(function (entries) {
    entries.forEach(function (entry) {
      if (!entry.isIntersecting) return;
      var el = entry.target;
      counters.unobserve(el);
      var target = parseInt(el.dataset.count, 10) || 0;
      if (reduced || target === 0) { el.textContent = String(target); return; }
      var start = performance.now();
      var span = 1100;
      (function tick(now) {
        var t = Math.min(1, (now - start) / span);
        var eased = 1 - Math.pow(1 - t, 3);
        el.textContent = String(Math.round(target * eased));
        if (t < 1) requestAnimationFrame(tick);
      })(start);
    });
  }, { threshold: 0.6 });
  document.querySelectorAll('[data-count]').forEach(function (el) { counters.observe(el); });

  /* ── Code panels ───────────────────────────────────────────────── */
  var tabs = document.querySelectorAll('.tabs [data-tab]');
  var panels = document.querySelectorAll('[data-panel]');

  function stagger(panel) {
    panel.querySelectorAll('.ln').forEach(function (line, i) {
      line.style.animation = 'none';
      // Force a reflow so the restarted animation actually replays.
      void line.offsetWidth;
      line.style.animation = '';
      line.style.animationDelay = i * 45 + 'ms';
    });
  }

  tabs.forEach(function (tab) {
    tab.addEventListener('click', function () {
      var name = tab.dataset.tab;
      tabs.forEach(function (t) { t.setAttribute('aria-selected', String(t === tab)); });
      panels.forEach(function (panel) {
        var active = panel.dataset.panel === name;
        panel.classList.toggle('is-active', active);
        if (active) stagger(panel);
      });
    });
  });

  var firstPanel = document.querySelector('.code.is-active');
  if (firstPanel) {
    var codeWatch = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        stagger(firstPanel);
        codeWatch.disconnect();
      });
    }, { threshold: 0.25 });
    codeWatch.observe(firstPanel);
  }

  /* ── Scroll rail, sticky nav, hero parallax ────────────────────── */
  var bar = document.querySelector('[data-progress]');
  var nav = document.querySelector('[data-nav]');
  var floater = document.querySelector('[data-float]');
  var ticking = false;

  function onScroll() {
    if (ticking) return;
    ticking = true;
    requestAnimationFrame(function () {
      var y = window.scrollY;
      var max = document.documentElement.scrollHeight - window.innerHeight;
      if (bar) bar.style.width = (max > 0 ? (y / max) * 100 : 0) + '%';
      if (nav) nav.classList.toggle('is-stuck', y > 40);
      // `translate` composes with the element's own transform animation.
      if (floater && !reduced && y < window.innerHeight * 1.2) {
        floater.style.translate = '0 ' + (y * 0.14).toFixed(1) + 'px';
      }
      ticking = false;
    });
  }

  window.addEventListener('scroll', onScroll, { passive: true });
  onScroll();

  /* ── Footer year ───────────────────────────────────────────────── */
  var year = document.querySelector('[data-year]');
  if (year) year.textContent = String(new Date().getFullYear());
})();
