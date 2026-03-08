import { login, changePassword } from '../lib/auth.js';
import { ApiError } from '../lib/api.js';

let mode = 'login'; // 'login' or 'set-password'
let keepMode = false; // prevent unmount from resetting mode during re-render

export function render(session) {
  if (session && !session.claimed) {
    mode = 'set-password';
  }

  if (mode === 'set-password') {
    return renderSetPassword();
  }

  return renderLogin();
}

function renderLogin() {
  return `
    <div class="auth-page">
      <div class="auth-card card">
        <header>
          <h2>Sign In</h2>
          <p>Enter your password to continue</p>
        </header>
        <form id="login-form" autocomplete="on">
          <label data-field>
            <span>Username</span>
            <input type="text" value="admin" readonly>
          </label>
          <label data-field>
            <span>Password</span>
            <input id="password-input" type="password" required autofocus>
          </label>
          <div id="login-error" role="alert" data-variant="error" hidden></div>
          <button id="login-btn" type="submit" class="btn btn-primary" style="width:100%;margin-top:0.5rem;">Sign In</button>
        </form>
      </div>
    </div>`;
}

function renderSetPassword() {
  return `
    <div class="auth-page">
      <div class="auth-card card">
        <header>
          <h2>Claim Your Device</h2>
          <p>Set a new password to secure your device</p>
        </header>
        <form id="set-password-form" autocomplete="off">
          <label data-field>
            <span>Generated Password</span>
            <input id="generated-password" type="password" placeholder="From device label or OLED" required>
            <small>Check the label on your device or the OLED display</small>
          </label>
          <label data-field>
            <span>New Password</span>
            <input id="new-password" type="password" minlength="8" placeholder="Min 8 characters" required>
          </label>
          <label data-field>
            <span>Confirm Password</span>
            <input id="confirm-password" type="password" placeholder="Re-enter new password" required>
            <small id="confirm-hint"></small>
          </label>
          <div id="set-password-error" role="alert" data-variant="error" hidden></div>
          <button id="set-password-btn" type="submit" class="btn btn-primary" style="width:100%;margin-top:0.5rem;" disabled>Set Password</button>
        </form>
      </div>
    </div>`;
}

export function mount(session) {
  if (mode === 'set-password') {
    mountSetPassword();
  } else {
    mountLogin();
  }
}

function mountLogin() {
  const form = document.getElementById('login-form');
  const passwordInput = document.getElementById('password-input');
  const errorEl = document.getElementById('login-error');
  const btn = document.getElementById('login-btn');

  if (!form) return;

  form.addEventListener('submit', async (e) => {
    e.preventDefault();

    const password = passwordInput.value;

    // Show loading state
    btn.setAttribute('aria-busy', 'true');
    btn.setAttribute('data-spinner', 'small');
    btn.disabled = true;
    errorEl.hidden = true;

    try {
      const result = await login('admin', password);

      if (result.password_change_required) {
        mode = 'set-password';
        keepMode = true;
        const { navigate } = await import('../app.js');
        navigate();
        return;
      }

      // Success — go to dashboard
      window.location.hash = '#dashboard';
      window.location.reload();
    } catch (err) {
      if (err instanceof ApiError && err.status === 401) {
        const remaining = err.data && err.data.attempts_remaining;
        errorEl.textContent = remaining != null
          ? `Invalid password. ${remaining} attempts remaining.`
          : 'Invalid password.';
        errorEl.hidden = false;
      } else {
        errorEl.textContent = err.message || 'Login failed.';
        errorEl.hidden = false;
      }
    } finally {
      btn.removeAttribute('aria-busy');
      btn.removeAttribute('data-spinner');
      btn.disabled = false;
    }
  });
}

function mountSetPassword() {
  const form = document.getElementById('set-password-form');
  const generatedInput = document.getElementById('generated-password');
  const newInput = document.getElementById('new-password');
  const confirmInput = document.getElementById('confirm-password');
  const hint = document.getElementById('confirm-hint');
  const errorEl = document.getElementById('set-password-error');
  const btn = document.getElementById('set-password-btn');

  if (!form) return;

  function validatePasswords() {
    const newVal = newInput.value;
    const confirmVal = confirmInput.value;

    if (!confirmVal) {
      hint.textContent = '';
      hint.style.color = '';
      btn.disabled = true;
      return;
    }

    if (newVal === confirmVal) {
      if (newVal.length >= 8) {
        hint.textContent = 'Passwords match';
        hint.style.color = 'var(--success)';
        btn.disabled = false;
      } else {
        hint.textContent = 'Minimum 8 characters';
        hint.style.color = 'var(--warning)';
        btn.disabled = true;
      }
    } else {
      hint.textContent = 'Passwords do not match';
      hint.style.color = 'var(--danger)';
      btn.disabled = true;
    }
  }

  newInput.addEventListener('input', validatePasswords);
  confirmInput.addEventListener('input', validatePasswords);

  form.addEventListener('submit', async (e) => {
    e.preventDefault();

    const generatedPassword = generatedInput.value;
    const newPassword = newInput.value;

    btn.setAttribute('aria-busy', 'true');
    btn.setAttribute('data-spinner', 'small');
    btn.disabled = true;
    errorEl.hidden = true;

    try {
      await changePassword(generatedPassword, newPassword);
      await login('admin', newPassword);
      window.location.hash = '#dashboard';
      window.location.reload();
    } catch (err) {
      if (err instanceof ApiError && err.status === 401) {
        errorEl.textContent = 'Generated password is incorrect.';
      } else {
        errorEl.textContent = err.message || 'Failed to set password.';
      }
      errorEl.hidden = false;
    } finally {
      btn.removeAttribute('aria-busy');
      btn.removeAttribute('data-spinner');
      btn.disabled = false;
      // Re-run validation to set correct button state
      validatePasswords();
    }
  });
}

export function unmount() {
  if (keepMode) {
    keepMode = false;
    return;
  }
  mode = 'login';
}
