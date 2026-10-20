// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton

import QtQuick
import org.kde.coreaddons as KCoreAddons

// Wording shared by the library lists
QtObject {
    function flavourLabel(flavour) {
        switch (flavour) {
        case "mini": return i18n("Introductions only");
        case "nopic": return i18n("Full text, no pictures");
        case "maxi": return i18n("Full, with pictures");
        default: return flavour;
        }
    }

    // Text from a ZIM, the catalog or the model, for the places that cannot be told to show plain text (tooltips, menu
    // items, buttons, combo boxes, placeholder messages): Qt reads a string as rich text when its first line holds anything
    // tag-like, and a ZIM title "<img src=http://…>" then fetched from the web and styled the app's own UI (the review,
    // 2026-10-19). "<" becomes a look-alike, so nothing can be read as markup.
    function plain(text) {
        return String(text ?? "").replace(/</g, "\u2039");
    }

    // A page title from outside text, for the breadcrumb: plain, and short enough that the trail keeps its parents
    // (a long project name was clipped mid-word and pushed the parent crumbs off; the review)
    function pageTitle(text) {
        const t = plain(text).trim();
        return t.length > 48 ? t.slice(0, 47).trim() + "…" : t;
    }

    // The plural argument must be a number: a formatted string such as "11,901" reads as 0
    function articles(count) {
        return i18np("%1 article", "%1 articles", Math.round(count));
    }

    // "Full text, no pictures · 52 GB · 6,912,345 articles · 2026-10-15"
    function metaLine(flavour, size, articleCount, date) {
        const parts = [];
        if (flavour)
            parts.push(flavourLabel(flavour));
        if (size > 0)
            parts.push(KCoreAddons.Format.formatByteSize(size, 1));
        if (articleCount > 0)
            parts.push(articles(articleCount));
        if (date)
            parts.push(date);
        return parts.join(" · ");
    }

    // A map's planet build ("20261019") as a date in the user's format
    function buildDate(build) {
        if (!/^\d{8}$/.test(build ?? ""))
            return build ?? "";
        const d = new Date(Number(build.slice(0, 4)), Number(build.slice(4, 6)) - 1, Number(build.slice(6, 8)));
        return d.toLocaleDateString(Qt.locale(), Locale.ShortFormat);
    }
}
