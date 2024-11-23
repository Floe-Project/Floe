// Populate the sidebar
//
// This is a script, and not included directly in the page, to control the total size of the book.
// The TOC contains an entry for each page, so if each page includes a copy of the TOC,
// the total size of the page becomes O(n**2).
class MDBookSidebarScrollbox extends HTMLElement {
    constructor() {
        super();
    }
    connectedCallback() {
        this.innerHTML = '<ol class="chapter"><li class="chapter-item expanded affix "><a href="home.html">Home</a></li><li class="chapter-item expanded "><div><strong aria-hidden="true">1.</strong> Installation</div></li><li><ol class="section"><li class="chapter-item expanded "><a href="installation/requirements.html"><strong aria-hidden="true">1.1.</strong> Requirements</a></li><li class="chapter-item expanded "><a href="installation/download-and-install-floe.html"><strong aria-hidden="true">1.2.</strong> Download &amp; Install Floe</a></li><li class="chapter-item expanded "><a href="installation/install-libraries-and-presets.html"><strong aria-hidden="true">1.3.</strong> Install Libraries &amp; Presets</a></li><li class="chapter-item expanded "><a href="installation/uninstalling.html"><strong aria-hidden="true">1.4.</strong> Uninstalling</a></li></ol></li><li class="chapter-item expanded "><div><strong aria-hidden="true">2.</strong> About</div></li><li><ol class="section"><li class="chapter-item expanded "><a href="about/mirage.html"><strong aria-hidden="true">2.1.</strong> Previously known as Mirage</a></li><li class="chapter-item expanded "><a href="about/sample-libraries.html"><strong aria-hidden="true">2.2.</strong> Sample Libraries</a></li><li class="chapter-item expanded "><a href="about/library-folders.html"><strong aria-hidden="true">2.3.</strong> Library Folders</a></li><li class="chapter-item expanded "><a href="about/preset-folders.html"><strong aria-hidden="true">2.4.</strong> Preset Folders</a></li><li class="chapter-item expanded "><a href="about/roadmap.html"><strong aria-hidden="true">2.5.</strong> Roadmap</a></li></ol></li><li class="chapter-item expanded "><div><strong aria-hidden="true">3.</strong> Developers</div></li><li><ol class="section"><li class="chapter-item expanded "><a href="develop/develop-libraries.html"><strong aria-hidden="true">3.1.</strong> Develop Libraries</a></li><li class="chapter-item expanded "><a href="develop/library-lua-api.html"><strong aria-hidden="true">3.2.</strong> Library Lua API</a></li><li class="chapter-item expanded "><a href="develop/develop-preset-packs.html"><strong aria-hidden="true">3.3.</strong> Develop Preset Packs</a></li><li class="chapter-item expanded "><a href="develop/packaging.html"><strong aria-hidden="true">3.4.</strong> Package Libraries &amp; Presets</a></li></ol></li><li class="chapter-item expanded "><li class="spacer"></li><li class="chapter-item expanded affix "><a href="changelog.html">Changelog</a></li></ol>';
        // Set the current, active page, and reveal it if it's hidden
        let current_page = document.location.href.toString();
        if (current_page.endsWith("/")) {
            current_page += "index.html";
        }
        var links = Array.prototype.slice.call(this.querySelectorAll("a"));
        var l = links.length;
        for (var i = 0; i < l; ++i) {
            var link = links[i];
            var href = link.getAttribute("href");
            if (href && !href.startsWith("#") && !/^(?:[a-z+]+:)?\/\//.test(href)) {
                link.href = path_to_root + href;
            }
            // The "index" page is supposed to alias the first chapter in the book.
            if (link.href === current_page || (i === 0 && path_to_root === "" && current_page.endsWith("/index.html"))) {
                link.classList.add("active");
                var parent = link.parentElement;
                if (parent && parent.classList.contains("chapter-item")) {
                    parent.classList.add("expanded");
                }
                while (parent) {
                    if (parent.tagName === "LI" && parent.previousElementSibling) {
                        if (parent.previousElementSibling.classList.contains("chapter-item")) {
                            parent.previousElementSibling.classList.add("expanded");
                        }
                    }
                    parent = parent.parentElement;
                }
            }
        }
        // Track and set sidebar scroll position
        this.addEventListener('click', function(e) {
            if (e.target.tagName === 'A') {
                sessionStorage.setItem('sidebar-scroll', this.scrollTop);
            }
        }, { passive: true });
        var sidebarScrollTop = sessionStorage.getItem('sidebar-scroll');
        sessionStorage.removeItem('sidebar-scroll');
        if (sidebarScrollTop) {
            // preserve sidebar scroll position when navigating via links within sidebar
            this.scrollTop = sidebarScrollTop;
        } else {
            // scroll sidebar to current active section when navigating via "next/previous chapter" buttons
            var activeSection = document.querySelector('#sidebar .active');
            if (activeSection) {
                activeSection.scrollIntoView({ block: 'center' });
            }
        }
        // Toggle buttons
        var sidebarAnchorToggles = document.querySelectorAll('#sidebar a.toggle');
        function toggleSection(ev) {
            ev.currentTarget.parentElement.classList.toggle('expanded');
        }
        Array.from(sidebarAnchorToggles).forEach(function (el) {
            el.addEventListener('click', toggleSection);
        });
    }
}
window.customElements.define("mdbook-sidebar-scrollbox", MDBookSidebarScrollbox);
