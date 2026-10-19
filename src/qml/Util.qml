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
}
