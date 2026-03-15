/**
 * Show a toast notification. Uses OAT's ot.toast if available, otherwise a DOM fallback.
 */
export function showToast(message, variant) {
  if (typeof ot !== 'undefined' && ot.toast) {
    ot.toast(message, '', variant ? { variant } : undefined);
    return;
  }
  // Fallback: simple DOM toast
  const toast = document.createElement('div');
  toast.setAttribute('role', 'status');
  toast.textContent = message;
  toast.style.cssText = 'position:fixed;bottom:1rem;left:50%;transform:translateX(-50%);background:var(--foreground);color:var(--background);padding:0.75rem 1.25rem;border-radius:var(--radius);z-index:9999;font-size:0.875rem;opacity:0;transition:opacity 0.3s;';
  document.body.appendChild(toast);
  requestAnimationFrame(() => { toast.style.opacity = '1'; });
  setTimeout(() => {
    toast.style.opacity = '0';
    setTimeout(() => toast.remove(), 300);
  }, 3000);
}
