# Jotila website

The public website for [Jotila](https://github.com/GarauGarau/Jotila), served at
**https://garaugarau.github.io/Jotila/** by GitHub Pages.

This independent `gh-pages` branch contains only the website. Application code
lives on [`windows`](https://github.com/GarauGarau/Jotila/tree/windows) (default)
and [`macos`](https://github.com/GarauGarau/Jotila/tree/macos).

## Structure

- `index.html`: introduction, platform choices, use cases and artwork credits.
- `windows.html`, `macos.html`: installer and portable downloads for each system.
- `style.css`: shared responsive layout, system fonts and note-window styling.
- `assets/`: local logo, optimized artwork, real app screenshots and MP4 demos.
- `.nojekyll`: serve these files directly, without a Jekyll build.

There is no build step, JavaScript, package manager, framework, tracking code,
external font or runtime dependency. Navigation, media controls and expandable
credits use native HTML. Videos request metadata only and never autoplay.

## Preview and publish

Clone just this branch:

```sh
git clone --branch gh-pages --single-branch https://github.com/GarauGarau/Jotila.git Jotila-site
cd Jotila-site
python -m http.server 8080
```

Open `http://localhost:8080`. Python is only one optional way to serve the files;
any static web server works.

GitHub Pages publishes from **gh-pages / (root)**. Commit and push to this branch
to update the website. Do not merge it into the application branches.

## Updating downloads and media

Update each platform's release version and its two asset links together in
`windows.html` or `macos.html`. Links point to explicit release tags: GitHub's
single `latest` release may not contain both sets of platform downloads.
Check each link against the published release assets before pushing.

The screenshots and recordings originate from `docs/screenshots` on `windows`.
WebP copies reduce image transfer size. MP4 copies use H.264, YUV420p and a
fast-start header for broad mobile playback support. Use a matching first-frame
poster when replacing a recording. Conversion tools are not site dependencies.

Before publishing changes, check narrow and wide layouts, keyboard navigation,
the platform-to-edition flow, four release downloads, media playback and credits.
All local URLs are relative so the site works under the `/Jotila/` project path.

See [artwork and media credits](CREDITS.md). Site code is [MIT-licensed](LICENSE).
