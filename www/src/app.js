import { initTheme } from './lib/theme.js';
import { checkSession } from './lib/auth.js';

initTheme();

const app = document.getElementById('app');
app.removeAttribute('aria-busy');
app.removeAttribute('data-spinner');

checkSession().then(session => {
  app.textContent = `Authenticated: ${session.authenticated}, Claimed: ${session.claimed}`;
});
