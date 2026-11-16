/* Interface navigation only. The instrument, saved data and USB protocol are unchanged. */
(() => {
  const pageList = document.getElementById('pages');
  const category = document.getElementById('studio-page-family');
  const current = document.getElementById('studio-current-page');
  if (!pageList || !category || !current) return;
  const update = () => {
    const family = category.value;
    for (const button of pageList.querySelectorAll('button')) {
      const active = button.classList.contains('active');
      const label = button.textContent.trim();
      const visible = family === 'ALL' || label.startsWith(family) || active;
      if (button.hidden === visible) button.hidden = !visible;
    }
    const active = pageList.querySelector('button.active');
    const label = active ? active.textContent.trim() : 'HOME · Live';
    const text = `Current page: ${label}`;
    if (current.textContent !== text) current.textContent = text;
  };
  category.addEventListener('change', update);
  new MutationObserver(update).observe(pageList, {
    childList: true, subtree: true, attributes: true, attributeFilter: ['class']
  });
  update();
  document.querySelector('.workspace nav')?.addEventListener('click', event => {
    const button = event.target.closest('[data-workspace]');
    if (!button) return;
    const destinations = {demo:'fm1-surface', edit:'editor', hardware:'device-editor'};
    const destination = document.getElementById(destinations[button.dataset.workspace]);
    requestAnimationFrame(() => destination?.scrollIntoView({
      block:'start', behavior:matchMedia('(prefers-reduced-motion: reduce)').matches ? 'auto' : 'smooth'
    }));
  });
})();
