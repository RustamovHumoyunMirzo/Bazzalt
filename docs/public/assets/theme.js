// Runs before paint; storage may be unavailable for local files/private browsing.
(() => {
  try {
    const saved = localStorage.getItem('bz-theme');
    if (saved === 'dark' || (!saved && matchMedia('(prefers-color-scheme: dark)').matches)) {
      document.documentElement.classList.add('dark');
    }
  } catch (_) {}
})();
