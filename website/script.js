const menuButton = document.querySelector(".menu-toggle");
const navigation = document.querySelector(".site-nav");

menuButton?.addEventListener("click", () => {
  const open = menuButton.getAttribute("aria-expanded") === "true";
  menuButton.setAttribute("aria-expanded", String(!open));
  menuButton.setAttribute("aria-label", open ? "Mở menu" : "Đóng menu");
  navigation?.classList.toggle("is-open", !open);
});

navigation?.addEventListener("click", (event) => {
  if (!(event.target instanceof HTMLAnchorElement)) {
    return;
  }
  menuButton?.setAttribute("aria-expanded", "false");
  menuButton?.setAttribute("aria-label", "Mở menu");
  navigation.classList.remove("is-open");
});

const copyButton = document.querySelector("[data-copy-target]");

copyButton?.addEventListener("click", async () => {
  const targetId = copyButton.getAttribute("data-copy-target");
  const target = targetId ? document.getElementById(targetId) : null;
  const label = copyButton.querySelector(".copy-label");

  if (!target || !label) {
    return;
  }

  try {
    await navigator.clipboard.writeText(target.innerText.replace(/^\$ /gm, ""));
    label.textContent = "Đã chép";
    window.setTimeout(() => {
      label.textContent = "Sao chép";
    }, 1800);
  } catch {
    label.textContent = "Không thể chép";
  }
});

const revealItems = document.querySelectorAll(".reveal");

if ("IntersectionObserver" in window) {
  const observer = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          entry.target.classList.add("is-visible");
          observer.unobserve(entry.target);
        }
      });
    },
    { threshold: 0.12 },
  );

  revealItems.forEach((item) => observer.observe(item));
} else {
  revealItems.forEach((item) => item.classList.add("is-visible"));
}
