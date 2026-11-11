const steps = [...document.querySelectorAll('.tour-steps .step')];
const links = [...document.querySelectorAll('.tour-select a')];

function showStep(id) {
  const selected = steps.find(step => step.id === id) || steps[0];
  for (const step of steps) step.hidden = step !== selected;
  for (const link of links) {
    if (link.hash === '#' + selected.id) link.setAttribute('aria-current', 'step');
    else link.removeAttribute('aria-current');
  }
}

function showLinkedStep() {
  const id = location.hash.slice(1);
  if (steps.some(step => step.id === id)) showStep(id);
}

for (const link of links) link.addEventListener('click', event => {
  if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
  event.preventDefault();
  showStep(link.hash.slice(1));
  if (location.hash !== link.hash) history.pushState(null, '', link.hash);
});
showStep(location.hash.slice(1));
window.addEventListener('hashchange', showLinkedStep);
window.addEventListener('popstate', showLinkedStep);
