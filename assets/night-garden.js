const garden = document.querySelector('.hero-visual');
if (garden) {
  const motion = window.matchMedia('(prefers-reduced-motion: reduce)');
  const toggle = garden.querySelector('.motion-toggle');
  let paused = motion.matches;
  const updateMotion = () => {
    garden.dataset.paused = String(paused || document.hidden);
    toggle.setAttribute('aria-pressed', String(paused));
    toggle.textContent = paused ? 'Animate artwork' : 'Pause artwork';
  };
  toggle.addEventListener('click', () => { paused = !paused; updateMotion(); });
  motion.addEventListener('change', e => { paused = e.matches; updateMotion(); });
  document.addEventListener('visibilitychange', updateMotion);
  for (const button of garden.querySelectorAll('[data-palette]')) {
    button.addEventListener('click', () => {
      garden.dataset.world = button.dataset.palette;
      for (const other of garden.querySelectorAll('[data-palette]')) {
        other.setAttribute('aria-pressed', String(other === button));
      }
    });
  }
  updateMotion();
}
