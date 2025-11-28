const menuButton = document.getElementById("menu-button");
const navLinks = document.getElementById("nav-links");
const closeMenu = document.getElementById("close-menu");

menuButton.addEventListener("click", () => {
    navLinks.classList.remove("-translate-x-full");
    navLinks.classList.add("translate-x-0");
});

closeMenu.addEventListener("click", () => {
    navLinks.classList.remove("translate-x-0");
    navLinks.classList.add("-translate-x-full");
});