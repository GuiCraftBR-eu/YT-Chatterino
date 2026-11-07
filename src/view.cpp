#include "view.hpp"
#include <algorithm>

namespace yc {
ChatView::ChatView(Hub *h, QWidget *p) : QAbstractScrollArea(p), hub(h) {
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    latest = new QPushButton(hub->cfg->T("↓ Latest"), viewport());
    latest->hide();
    latest->setFocusPolicy(Qt::NoFocus);
    connect(latest, &QPushButton::clicked, this, &ChatView::jumpLatest);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (!hoverSuspended) hideEmoteTooltip();
        latest->setVisible(!follow && verticalScrollBar()->maximum() > 0);
        viewport()->update();
    });
    resizeTimer.setSingleShot(true);
    resizeTimer.setInterval(24);
    connect(&resizeTimer, &QTimer::timeout, this, &ChatView::reflow);
    autoScrollTimer.setInterval(16);
    autoScrollTimer.setTimerType(Qt::PreciseTimer);
    connect(&autoScrollTimer, &QTimer::timeout, this, [this] {
        const auto elapsed = qBound<qint64>(qint64(1), autoScrollClock.restart(), qint64(100));
        advanceAutoScroll(viewport()->mapFromGlobal(QCursor::pos()), elapsed / 1000.0);
    });
    connect(hub->cfg, &Config::appearanceChanged, this, &ChatView::invalidate);
    connect(hub->cfg, &Config::appearanceChanged, this, &ChatView::updateHoverPause);
    connect(hub->cfg, &Config::languageChanged, this, &ChatView::invalidate);
    connect(hub->images, &Images::ready, this, [this](const QString &url) {
        if (emoteTooltip && emoteTooltip->isVisible() && tooltipUrls.value(0) == url) {
            const auto image = hub->images->get(tooltipUrls, false);
            if (!image.isNull()) emotePreview->setPixmap(QPixmap::fromImage(image).scaled(
                tooltipImageSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            emoteTooltip->adjustSize();
        }
        bool changed = false;
        for (auto i = details.begin(); i != details.end();) {
            if (i->urls.contains(url)) {
                documents.remove(i.key());
                i = details.erase(i);
                changed = true;
            } else {
                ++i;
            }
        }
        if (changed) {
            viewport()->update();
        }
    });
    connect(hub->images, &Images::frame, this, [this](const QString &url) {
        if (!isVisible()) {
            return;
        }
        if (emoteTooltip && emoteTooltip->isVisible() && tooltipUrls.value(0) == url) {
            const auto preview = hub->images->get(tooltipUrls, true);
            if (!preview.isNull()) {
                emotePreview->setPixmap(QPixmap::fromImage(preview).scaled(tooltipImageSize, Qt::KeepAspectRatio,
                                                                           Qt::SmoothTransformation));
            }
        }
        int begin = qMax(0, rowAt(verticalScrollBar()->value()));
        for (int row = begin; row < messages.size() &&
                              offsets.value(row) < verticalScrollBar()->value() + viewport()->height();
             ++row) {
            auto detail = details.value(messages[row].seq);
            if (detail.urls.contains(url)) {
                auto doc = documents.value(messages[row].seq);
                if (doc) {
                    auto image = hub->images->get({url});
                    if (!image.isNull()) {
                        doc->addResource(QTextDocument::ImageResource, QUrl(url),
                                         image.scaled(detail.imageSizes.value(url, image.size()),
                                                      Qt::KeepAspectRatio, Qt::FastTransformation));
                        details[messages[row].seq].imageKeys[url] = image.cacheKey();
                    }
                }
                viewport()->update();
            }
        }
    });
}

void ChatView::setMessages(QVector<Message> list) {
    qint64 firstSelected = selectionStart.first >= 0 && selectionStart.first < messages.size()
                               ? messages[selectionStart.first].seq
                               : 0;
    qint64 lastSelected = selectionEnd.first >= 0 && selectionEnd.first < messages.size()
                              ? messages[selectionEnd.first].seq
                              : 0;
    int oldTop = rowAt(verticalScrollBar()->value());
    qint64 anchor = oldTop >= 0 ? messages[oldTop].seq : 0;
    int delta = oldTop >= 0 ? verticalScrollBar()->value() - offsets.value(oldTop) : 0;
    messages = std::move(list);
    if (messages.isEmpty()) {
        stopAutoScroll();
    }
    for (auto &m : messages) {
        if (!m.seq) {
            m.seq = synthetic--;
        }
    }
    bool stripe = false;
    for (const auto &m : messages) {
        if (rowStripes.contains(m.seq)) stripe = rowStripes.value(m.seq);
        else rowStripes.insert(m.seq, stripe);
        stripe = !stripe;
    }
    documents.clear();
    documentUse.clear();
    details.clear();
    heights.clear();
    for (const auto &m : messages) {
        heights << estimate(m);
    }
    trim();
    QSet<qint64> retained;
    for (const auto &m : messages) retained.insert(m.seq);
    for (auto it = rowStripes.begin(); it != rowStripes.end();) {
        if (!retained.contains(it.key())) it = rowStripes.erase(it);
        else ++it;
    }
    auto locate = [this](qint64 seq) {
        for (int i = 0; i < messages.size(); ++i) {
            if (messages[i].seq == seq) {
                return i;
            }
        }
        return -1;
    };
    selectionStart.first = firstSelected ? locate(firstSelected) : -1;
    selectionEnd.first = lastSelected ? locate(lastSelected) : -1;
    if (selectionStart.first < 0 || selectionEnd.first < 0) {
        selectionStart = selectionEnd = {-1, 0};
    }
    rebuildOffsets();
    if (follow) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    } else if (anchor) {
        for (int i = 0; i < messages.size(); ++i) {
            if (messages[i].seq == anchor) {
                verticalScrollBar()->setValue(offsets.value(i) + delta);
                break;
            }
        }
    }
    viewport()->update();
}

void ChatView::append(Message m) {
    if (!m.seq) {
        m.seq = synthetic--;
    }
    if (!rowStripes.contains(m.seq)) {
        rowStripes.insert(m.seq, !messages.isEmpty() && !rowStripes.value(messages.last().seq));
    }
    const int oldCount = messages.size();
    messages << m;
    heights << estimate(m);
    if (messages.size() <= messageCap && offsets.size() == oldCount + 1) {
        offsets << offsets.last() + heights.last();
        verticalScrollBar()->setRange(0, qMax(0, offsets.last() - viewport()->height()));
        verticalScrollBar()->setPageStep(viewport()->height());
    } else {
        trim();
        rebuildOffsets();
    }
    if (follow) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
    viewport()->update();
}

void ChatView::trim() {
    int extra = messages.size() - messageCap;
    if (extra <= 0) {
        return;
    }
    int pixels = 0;
    for (int i = 0; i < extra; ++i) {
        pixels += heights.value(i);
        documents.remove(messages[i].seq);
        documentUse.remove(messages[i].seq);
        details.remove(messages[i].seq);
        rowStripes.remove(messages[i].seq);
    }
    messages.remove(0, extra);
    heights.remove(0, qMin(extra, int(heights.size())));
    selectionStart.first -= extra;
    selectionEnd.first -= extra;
    if (selectionStart.first < 0 || selectionEnd.first < 0) {
        selectionStart = selectionEnd = {-1, 0};
    }
    if (!follow) {
        verticalScrollBar()->setValue(qMax(0, verticalScrollBar()->value() - pixels));
    }
}

int ChatView::estimate(const Message &m) const {
    const auto a = hub->cfg->appearance();
    const QFontMetrics fm(hub->cfg->messageFont());
    int width = qMax(120, viewport()->width() - 14);
    QString text = m.name + ": " + visibleMessageText(m, *hub->cfg) + deletionReasonText(m, *hub->cfg);
    int lines = qMax(1, (fm.horizontalAdvance(text) + width - 1) / width);
    lines += text.count('\n');
    int height = lines * fm.lineSpacing() + 8;
    if (!m.system && (m.special == "alert" || m.special == "member" || m.special == "super")) {
        const bool hasBody = !m.alertText.isEmpty() && m.alertBodyStart < m.text.size();
        height += fm.lineSpacing() * (hasBody ? 2 : 1);
    }
    if (!m.reply.isEmpty() && !threadMode) {
        height += fm.lineSpacing();
    }
    if (!m.emotes.isEmpty() && (!m.deleted || hub->cfg->data["show_deleted_messages"].toBool(false))) {
        height = qMax(height, a["emote_size"].toInt(36) + 8);
    }
    return height;
}

void ChatView::rebuildOffsets() {
    offsets.resize(heights.size() + 1);
    offsets[0] = 0;
    for (int i = 0; i < heights.size(); ++i) {
        offsets[i + 1] = offsets[i] + heights[i];
    }
    verticalScrollBar()->setRange(0, qMax(0, offsets.last() - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
}

void ChatView::invalidate() {
    hideEmoteTooltip();
    int top = rowAt(verticalScrollBar()->value()), delta = verticalScrollBar()->value() - offsets.value(top);
    documents.clear();
    documentUse.clear();
    details.clear();
    heights.clear();
    for (const auto &m : messages) {
        heights << estimate(m);
    }
    rebuildOffsets();
    if (follow) {
        jumpLatest();
    } else {
        verticalScrollBar()->setValue(offsets.value(top) + delta);
    }
    viewport()->update();
}

void ChatView::reflow() {
    layoutWidth = viewport()->width();
    invalidate();
}

int ChatView::rowAt(int y) const {
    if (messages.isEmpty() || offsets.isEmpty()) {
        return -1;
    }
    auto i = std::upper_bound(offsets.begin(), offsets.end(), y);
    return qBound(0, int(i - offsets.begin()) - 1, int(messages.size()) - 1);
}

static QTextCharFormat format(const QFont &font, QColor color) {
    QTextCharFormat f;
    f.setFont(font);
    f.setForeground(color);
    return f;
}

QImage channelIcon(Source *source, Images *images, int size) {
    QImage result(size, size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath circle;
    circle.addEllipse(QRectF(0, 0, size, size));
    painter.setClipPath(circle);
    const auto url = source ? str(source->header.value("avatar")) : QString();
    const auto image = url.isEmpty() ? QImage() : images->get({url}, false);
    if (!image.isNull()) {
        const auto scaled = image.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        painter.drawImage(QPoint((size - scaled.width()) / 2, (size - scaled.height()) / 2), scaled);
    } else {
        painter.fillRect(result.rect(), QColor("#353540"));
        painter.setPen(Qt::white);
        QFont font("Segoe UI", qMax(7, size / 2), QFont::Bold);
        painter.setFont(font);
        painter.drawText(result.rect(), Qt::AlignCenter, source ? source->name().left(1).toUpper() : "?");
    }
    return result;
}

static QImage roleBadge(const QString &role, int size) {
    QImage image(size * 2, size * 2, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(image.width() / 18., image.height() / 18.);
    p.setPen(Qt::NoPen);
    if (role == "verified") {
        QPainterPath seal;
        constexpr double pi = 3.141592653589793;
        for (int i = 0; i <= 144; ++i) {
            const double angle = i * 2 * pi / 144;
            const double radius = 8.0 + 0.8 * std::cos(12 * angle);
            QPointF point(9 + radius * std::cos(angle), 9 + radius * std::sin(angle));
            if (i == 0) seal.moveTo(point); else seal.lineTo(point);
        }
        seal.closeSubpath();
        p.setBrush(QColor("#009fe3"));
        p.drawPath(seal);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 2.1, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.drawPolyline(QPolygonF{{4.2, 9.2}, {7.4, 12.2}, {13.8, 5.8}});
        return image;
    }
    p.setBrush(QColor(role == "moderator" ? "#00ad03"
                      : role == "owner"   ? "#e91916"
                      : role == "vip"     ? "#e005b9"
                                          : "#7349b8"));
    p.drawRoundedRect(QRectF(0, 0, 18, 18), 2, 2);
    p.setBrush(Qt::white);
    if (role == "owner") {
        p.drawPolygon(QPolygonF{{3, 6}, {6, 9}, {9, 4}, {12, 9}, {15, 6}, {14, 14}, {4, 14}});
    } else if (role == "moderator") {
        p.drawPolygon(QPolygonF{{5, 3}, {10, 4}, {15, 9}, {12, 12}, {7, 7}});
        p.setPen(QPen(Qt::white, 2));
        p.drawLine(4, 14, 10, 8);
        p.drawLine(3, 10, 8, 15);
    } else if (role == "vip") {
        p.drawPolygon(QPolygonF{{9, 3}, {15, 9}, {9, 15}, {3, 9}});
    } else {
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            double angle = (i * 36 - 90) * 3.141592653589793 / 180;
            double radius = i % 2 ? 2.7 : 6.5;
            star << QPointF(9 + radius * std::cos(angle), 9 + radius * std::sin(angle));
        }
        p.drawPolygon(star);
    }
    return image;
}

QSharedPointer<QTextDocument> ChatView::document(int row) {
    if (row < 0 || row >= messages.size()) {
        return {};
    }
    const auto &m = messages[row];
    documentUse[m.seq] = ++documentClock;
    auto existing = documents.value(m.seq);
    if (existing) {
        return existing;
    }
    auto doc = QSharedPointer<QTextDocument>::create();
    doc->setDocumentMargin(0);
    doc->setDefaultFont(hub->cfg->messageFont());
    doc->setTextWidth(qMax(100, viewport()->width() - 14));
    QTextCursor cursor(doc.data());
    Details detail;
    const auto a = hub->cfg->appearance();
    const auto normal = hub->cfg->messageFont(), username = hub->cfg->usernameFont();
    QColor fg("#efeff1"), dim("#8b8b94");
    auto normalFormat = format(normal, fg);
    auto source = hub->sources.value(m.source);
    const bool isAlert = !m.system && (m.special == "alert" || m.special == "member" || m.special == "super");
    const int bodyOffset = isAlert && !m.alertText.isEmpty() ? qBound(0, m.alertBodyStart, int(m.text.size())) : 0;
    if (!m.reply.isEmpty() && !threadMode) {
        auto rf = format(QFont(normal.family(), qMax(8, normal.pointSize() - 2)), dim);
        QString name = str(m.reply["name"]);
        if (name.isEmpty()) {
            name = str(m.reply["login"]);
        }
        cursor.insertText(hub->cfg->T("↪ Replying to @{name}: {body}",
                                      {{"name", name}, {"body", str(m.reply["text"]).left(180)}}) +
                              '\n',
                          rf);
        detail.replyEnd = cursor.position();
    }
    if (isAlert) {
        const QColor accent = source && source->platform == "kick" ? QColor("#53fc18")
                            : source && source->platform == "youtube" ? QColor("#ff7777") : QColor("#bf94ff");
        const auto eventIcon = alertIcon(m.alertKind);
        if (!eventIcon.isNull()) {
            const int height = qMax(14, QFontMetrics(username).height() - 2);
            const QSize size = eventIcon.size().scaled(height + 2, height, Qt::KeepAspectRatio);
            const QString iconUrl = "icon:alert-" + (m.alertKind.isEmpty() ? QString("other") : m.alertKind);
            doc->addResource(QTextDocument::ImageResource, QUrl(iconUrl), eventIcon);
            QTextImageFormat icon; icon.setName(iconUrl);
            icon.setWidth(size.width()); icon.setHeight(size.height());
            icon.setVerticalAlignment(QTextCharFormat::AlignMiddle);
            detail.imageCodes[cursor.position()] = "[Alert]";
            cursor.insertImage(icon); cursor.insertText(" ", normalFormat);
        }
        detail.nameStart = cursor.position();
        cursor.insertText(m.name, format(username, accent));
        detail.nameEnd = cursor.position();
        const QString description = m.alertText.isEmpty() ? visibleMessageText(m, *hub->cfg) : visibleAlertText(m, *hub->cfg);
        cursor.insertText("\n    " + description, normalFormat);
        const bool hasBody = !m.alertText.isEmpty() && bodyOffset < m.text.size() &&
                             (!m.deleted || hub->cfg->data["show_deleted_messages"].toBool(false));
        if (!hasBody) {
            const auto reason = deletionReasonText(m, *hub->cfg);
            if (!reason.isEmpty()) cursor.insertText(" · " + reason, normalFormat);
            documents[m.seq] = doc;
            details[m.seq] = detail;
            return doc;
        }
        cursor.insertText("\n", normalFormat);
    }
    if (a["show_timestamps"].toBool(true) && m.stamp.isValid()) {
        cursor.insertText(hub->cfg->time(m.stamp) + ' ', format(hub->cfg->timestampFont(), dim));
    }
    if (m.system) {
        cursor.insertText(hub->cfg->T(m.text), format(normal, dim));
        documents[m.seq] = doc;
        details[m.seq] = detail;
        return doc;
    }
    auto insertImage = [&](QStringList urls, QString code, int size, bool gif, bool isEmote = false,
                           bool isBadge = false) {
        if (urls.isEmpty()) {
            cursor.insertText(code + ' ', format(normal, QColor("#bf94ff")));
            return;
        }
        auto image = hub->images->get(urls, isVisible());
        detail.urls.insert(urls[0]);
        if (image.isNull()) {
            cursor.insertText(code, format(normal, QColor("#bf94ff")));
            return;
        }
        QSize fitted = image.size();
        fitted.scale(gif ? QSize(180, 180) : QSize(size * 3, size), Qt::KeepAspectRatio);
        QTextImageFormat f;
        f.setName(urls[0]);
        f.setWidth(fitted.width());
        f.setHeight(fitted.height());
        f.setVerticalAlignment(QTextCharFormat::AlignMiddle);
        detail.imageCodes[cursor.position()] = code;
        if (isEmote && !gif) {
            detail.emoteImages[cursor.position()] = {code, urls};
        }
        if (isBadge) detail.badgeImages[cursor.position()] = {code, urls};
        detail.imageSizes[urls[0]] = fitted;
        detail.imageKeys[urls[0]] = image.cacheKey();
        doc->addResource(QTextDocument::ImageResource, QUrl(urls[0]),
                         image.scaled(fitted, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        cursor.insertImage(f);
    };
    if (mixed && source) {
        const auto url = str(source->header.value("avatar"));
        const QString resource = url.isEmpty() ? "channel:" + source->key : url;
        if (!url.isEmpty()) detail.urls.insert(url);
        const auto icon = channelIcon(source, hub->images, 24);
        doc->addResource(QTextDocument::ImageResource, QUrl(resource), icon);
        QTextImageFormat channelFormat;
        channelFormat.setName(resource);
        channelFormat.setWidth(18);
        channelFormat.setHeight(18);
        channelFormat.setVerticalAlignment(QTextCharFormat::AlignMiddle);
        detail.imageCodes[cursor.position()] = '[' + source->name() + ']';
        detail.channelImages[cursor.position()] = {source->name(), {resource}};
        cursor.insertImage(channelFormat);
        cursor.insertText(" ", normalFormat);
    }
    for (auto badge : m.badges) {
        if (badge.urls.isEmpty() && source) {
            auto full = source->badges.value(badge.id + '/' + badge.version);
            if (!full.urls.isEmpty()) {
                badge = full;
            }
        }
        QString label = badge.label;
        if (label.isEmpty() || (label.startsWith('[') && label.endsWith(']'))) {
            if (badge.role == "owner") label = hub->cfg->T(source && source->platform == "twitch" ? "Broadcaster" : "Owner");
            else if (badge.role == "moderator") label = hub->cfg->T("Moderator");
            else if (badge.role == "subscriber") label = hub->cfg->T("Subscriber");
            else if (badge.role == "vip") label = "VIP";
            else if (badge.role == "verified") label = hub->cfg->T("Verified");
            else { label = badge.id; label.replace('-', ' '); label.replace('_', ' '); }
        }
        const bool youtubeVerified = source && source->platform == "youtube" && badge.role == "verified";
        if (youtubeVerified || (badge.urls.isEmpty() && (badge.role == "owner" || badge.role == "moderator" ||
                                                        badge.role == "subscriber" || badge.role == "vip"))) {
            const int size = a["badge_size"].toInt(18);
            QString url = "badge:" + badge.role;
            doc->addResource(QTextDocument::ImageResource, QUrl(url), roleBadge(badge.role, size));
            QTextImageFormat f;
            f.setName(url);
            f.setWidth(size);
            f.setHeight(size);
            f.setVerticalAlignment(QTextCharFormat::AlignMiddle);
            detail.imageCodes[cursor.position()] = '[' + badge.role + ']';
            detail.badgeImages[cursor.position()] = {label, {url}};
            cursor.insertImage(f);
            cursor.insertText(" ", normalFormat);
        } else {
            insertImage(badge.urls, label.isEmpty() ? '[' + badge.id + ']' : label,
                        a["badge_size"].toInt(18), false, false, true);
            cursor.insertText(" ", normalFormat);
        }
    }
    detail.nameStart = cursor.position();
    cursor.insertText(m.name, format(username, userColor(m)));
    detail.nameEnd = cursor.position();
    cursor.insertText(": ", normalFormat);
    const bool revealDeleted = m.deleted && hub->cfg->data["show_deleted_messages"].toBool(false);
    QString body = visibleMessageText(m, *hub->cfg);
    QVector<Span> spans = m.deleted && !revealDeleted ? QVector<Span>() : m.emotes;
    if (isAlert && bodyOffset > 0 && (!m.deleted || revealDeleted)) {
        body.remove(0, bodyOffset);
        for (auto &span : spans) { span.start -= bodyOffset; span.end -= bodyOffset; }
    }
    if (source && (!m.deleted || revealDeleted)) {
        auto matches = QRegularExpression("\\S+").globalMatch(body);
        while (matches.hasNext()) {
            auto match = matches.next();
            auto it = source->emotes.constFind(match.captured());
            if (it != source->emotes.cend()) {
                auto span = it.value();
                span.start = match.capturedStart();
                span.end = match.capturedEnd();
                span.code = match.captured();
                spans << span;
            }
        }
    }
    std::stable_sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) {
        return a.start == b.start ? a.end > b.end : a.start < b.start;
    });
    int offset = 0;
    auto insertPlain = [&](const QString &text, int bodyStart) {
        static const QRegularExpression mentions(
            "(?<![\\p{L}\\p{N}_@])@[\\p{L}\\p{N}_][\\p{L}\\p{N}_.-]{0,63}");
        QVector<MentionSpan> candidates;
        if (!m.deleted || revealDeleted) for (const auto &mention : m.mentions) {
            const int start = mention.start - (isAlert ? bodyOffset : 0);
            const int end = mention.end - (isAlert ? bodyOffset : 0);
            if (start >= bodyStart && end <= bodyStart + text.size() && end > start)
                candidates.append({start - bodyStart, end - bodyStart, mention.name, mention.uid});
        }
        auto matches = mentions.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            QString value = match.captured();
            while (value.endsWith('.')) value.chop(1);
            if (value.size() < 2) continue;
            const int start = match.capturedStart(), end = start + value.size();
            bool overlap = false;
            for (const auto &candidate : candidates)
                if (start < candidate.end && end > candidate.start) { overlap = true; break; }
            if (!overlap) candidates.append({start, end, value.mid(1), {}});
        }
        std::sort(candidates.begin(), candidates.end(), [](const MentionSpan &a, const MentionSpan &b) {
            return a.start < b.start;
        });
        int from = 0;
        for (const auto &candidate : candidates) {
            if (candidate.start < from || candidate.end > text.size()) continue;
            cursor.insertText(text.mid(from, candidate.start - from), normalFormat);
            const int start = cursor.position();
            cursor.insertText(text.mid(candidate.start, candidate.end - candidate.start),
                              format(normal, QColor("#94c9ff")));
            detail.mentions.append({start, cursor.position(), candidate.name, candidate.uid});
            from = candidate.end;
        }
        cursor.insertText(text.mid(from), normalFormat);
    };
    auto insertText = [&](const QString &text, int bodyStart) {
        auto links = QRegularExpression("https?://[^\\s<>]+").globalMatch(text);
        int from = 0;
        while (links.hasNext()) {
            auto match = links.next();
            insertPlain(text.mid(from, match.capturedStart() - from), bodyStart + from);
            auto lf = normalFormat;
            lf.setAnchor(true);
            lf.setAnchorHref(match.captured());
            lf.setForeground(QColor("#4a9cdb"));
            cursor.insertText(match.captured(), lf);
            from = match.capturedEnd();
        }
        insertPlain(text.mid(from), bodyStart + from);
    };
    for (const auto &e : spans) {
        if (e.start < offset || e.end > body.size() || e.end <= e.start) {
            continue;
        }
        insertText(body.mid(offset, e.start - offset), offset);
        insertImage(e.urls, e.code.isEmpty() ? body.mid(e.start, e.end - e.start) : e.code,
                    a["emote_size"].toInt(36), e.gif, true);
        offset = e.end;
    }
    insertText(body.mid(offset), offset);
    const auto reason = deletionReasonText(m, *hub->cfg);
    if (!reason.isEmpty()) cursor.insertText(" · " + reason, normalFormat);
    if (!query.isEmpty()) {
        QTextCursor found(doc.data());
        while (!(found = doc->find(query, found)).isNull()) {
            auto sf = found.charFormat();
            sf.setBackground(QColor("#806500"));
            sf.setForeground(Qt::white);
            found.mergeCharFormat(sf);
        }
    }
    documents[m.seq] = doc;
    details[m.seq] = detail;
    if (documents.size() > 128) {
        auto keep = messages.value(row).seq;
        qint64 key = 0, oldest = LLONG_MAX;
        for (auto i = documentUse.begin(); i != documentUse.end(); ++i) {
            if (i.key() != keep && documents.contains(i.key()) && i.value() < oldest) {
                oldest = i.value();
                key = i.key();
            }
        }
        documents.remove(key);
        documentUse.remove(key);
        details.remove(key);
    }
    return doc;
}

void ChatView::paintEvent(QPaintEvent *) {
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), Qt::black);
    if (messages.isEmpty()) {
        return;
    }
    int scroll = verticalScrollBar()->value(), row = rowAt(scroll);
    bool changed = false;
    auto a = hub->cfg->appearance();
    int y = offsets.value(qMax(0, row)) - scroll;
    for (int i = qMax(0, row); i < messages.size() && y < viewport()->height(); ++i) {
        auto doc = document(i);
        if (!doc) {
            continue;
        }
        const auto detail = details.value(messages[i].seq);
        for (auto it = detail.imageSizes.begin(); it != detail.imageSizes.end(); ++it) {
            auto image = hub->images->get({it.key()});
            if (!image.isNull() && detail.imageKeys.value(it.key()) != image.cacheKey()) {
                doc->addResource(QTextDocument::ImageResource, QUrl(it.key()),
                                 image.scaled(it.value(), Qt::KeepAspectRatio, Qt::FastTransformation));
                details[messages[i].seq].imageKeys[it.key()] = image.cacheKey();
            }
        }
        int height = qCeil(doc->size().height()) + (a["compact_spacing"].toBool() ? 4 : 8);
        if (height != heights[i]) {
            heights[i] = height;
            changed = true;
        }
        QRect rect(0, y, viewport()->width(), height);
        QColor background = highlight(messages[i], *hub->cfg, hub->accounts->names());
        if (!background.isValid()) {
            background = a["alternate_rows"].toBool(true) && rowStripes.value(messages[i].seq)
                             ? QColor("#0e0e0e") : QColor(Qt::black);
        }
        painter.fillRect(rect, background);
        if (!messages[i].system && (messages[i].special == "alert" || messages[i].special == "member" ||
                                    messages[i].special == "super")) {
            auto *origin = hub->sources.value(messages[i].source);
            painter.fillRect(QRect(0, y, 4, height), origin && origin->platform == "youtube" ? QColor("#ff7777")
                             : origin && origin->platform == "kick" ? QColor("#53fc18") : QColor("#bf94ff"));
        }
        painter.save();
        painter.translate(6, y + 3);
        if (messages[i].deleted && hub->cfg->data["show_deleted_messages"].toBool(false)) {
            painter.setOpacity(0.5);
        }
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, QColor("#efeff1"));
        auto start = selectionStart, end = selectionEnd;
        if (start > end) {
            std::swap(start, end);
        }
        if (start.first >= 0 && i >= start.first && i <= end.first) {
            QAbstractTextDocumentLayout::Selection sel;
            sel.cursor = QTextCursor(doc.data());
            sel.cursor.setPosition(i == start.first ? qBound(0, start.second, doc->characterCount() - 1) : 0);
            sel.cursor.setPosition(i == end.first ? qBound(0, end.second, doc->characterCount() - 1)
                                                  : doc->characterCount() - 1,
                                   QTextCursor::KeepAnchor);
            sel.format.setBackground(QColor("#365f85"));
            sel.format.setForeground(Qt::white);
            context.selections << sel;
        }
        doc->documentLayout()->draw(&painter, context);
        painter.restore();
        y += height;
    }
    if (changed) {
        rebuildOffsets();
        if (follow) verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        viewport()->update();
    }
    if (autoScrolling) {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#aaaaaa"), 1));
        painter.setBrush(QColor("#181818"));
        painter.drawEllipse(autoScrollAnchor, 12, 12);
        painter.setBrush(QColor("#dddddd"));
        const QPoint c = autoScrollAnchor;
        painter.drawPolygon(QPolygon{c + QPoint(-4, -3), c + QPoint(4, -3), c + QPoint(0, -8)});
        painter.drawPolygon(QPolygon{c + QPoint(-4, 3), c + QPoint(4, 3), c + QPoint(0, 8)});
    }
}

void ChatView::resizeEvent(QResizeEvent *e) {
    QAbstractScrollArea::resizeEvent(e);
    resizeTimer.start();
    latest->adjustSize();
    latest->move(qMax(0, viewport()->width() - latest->width() - 12),
                 qMax(0, viewport()->height() - latest->height() - 8));
}

QPair<int, int> ChatView::positionAt(QPoint point) {
    int row = rowAt(point.y() + verticalScrollBar()->value());
    if (row < 0) {
        return {-1, 0};
    }
    auto doc = document(row);
    int position = doc->documentLayout()->hitTest(
        QPointF(point.x() - 6, point.y() + verticalScrollBar()->value() - offsets.value(row) - 3),
        Qt::FuzzyHit);
    return {row, qBound(0, position, doc->characterCount() - 1)};
}

ChatView::Details::Mention ChatView::mentionAt(int row, QPoint point) {
    if (row < 0 || row >= messages.size()) return {};
    auto doc = document(row);
    const int hit = doc->documentLayout()->hitTest(
        QPointF(point.x() - 6, point.y() + verticalScrollBar()->value() - offsets.value(row) - 3),
        Qt::ExactHit);
    for (const auto &mention : details.value(messages[row].seq).mentions) {
        if (hit >= mention.start && hit < mention.end) return mention;
    }
    return {};
}

void ChatView::updateHoverPause() {
    const bool pause = (hoverInside && hub->cfg->data["pause_on_hover"].toBool(false)) || previewHolding;
    if (pause && !hoverSuspended) {
        hoverSuspended = true;
        hoverResumeFollow = follow;
        follow = false;
    } else if (!pause && hoverSuspended) {
        hoverSuspended = false;
        if (hoverResumeFollow) {
            follow = true;
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
            latest->hide();
        }
        hoverResumeFollow = false;
    }
}

void ChatView::hideEmoteTooltip() {
    previewHolding = false;
    if (emoteTooltip) {
        emoteTooltip->hide();
    }
    tooltipUrls.clear();
    streamPreviewSource.clear();
    tooltipMessage = 0;
    tooltipPosition = -1;
    updateHoverPause();
}

bool ChatView::showEmoteTooltip(QPoint point, QPoint global) {
    const int y = point.y() + verticalScrollBar()->value();
    if (offsets.isEmpty() || y < 0 || y >= offsets.last()) {
        hideEmoteTooltip();
        return false;
    }
    const int row = rowAt(y);
    auto doc = document(row);
    const auto detail = details.value(messages[row].seq);
    auto info = detail.emoteImages;
    for (auto i = detail.badgeImages.cbegin(); i != detail.badgeImages.cend(); ++i) info.insert(i.key(), i.value());
    for (auto i = detail.channelImages.cbegin(); i != detail.channelImages.cend(); ++i) info.insert(i.key(), i.value());
    if (info.isEmpty()) {
        hideEmoteTooltip();
        return false;
    }
    (void)doc->size();
    const QPointF local(point.x() - 6, y - offsets.value(row) - 3);
    const auto containsImage = [&](int candidate) {
        const auto block = doc->findBlock(candidate);
        if (!block.isValid() || !block.layout()) return false;
        const auto line = block.layout()->lineForTextPosition(candidate - block.position());
        if (!line.isValid()) return false;
        const auto bounds = doc->documentLayout()->blockBoundingRect(block);
        const qreal x1 = line.cursorToX(candidate - block.position());
        const qreal x2 = line.cursorToX(candidate - block.position() + 1);
        return QRectF(bounds.left() + qMin(x1, x2), bounds.top() + line.y(),
                      qAbs(x2 - x1), line.height()).adjusted(-0.5, -0.5, 0.5, 0.5).contains(local);
    };
    const int hit = doc->documentLayout()->hitTest(local, Qt::ExactHit);
    int position = -1;
    if (info.contains(hit) && containsImage(hit)) position = hit;
    else if (info.contains(hit - 1) && containsImage(hit - 1)) position = hit - 1;
    else {
        // Qt's exact hit test can miss an inline image in wrapped/multiline
        // alert text. Check the actual inline image bounds as a fallback.
        for (auto it = info.cbegin(); it != info.cend(); ++it)
            if (containsImage(it.key())) { position = it.key(); break; }
    }
    if (position < 0) {
        hideEmoteTooltip();
        return false;
    }
    previewHolding = true;
    updateHoverPause();
    if (emoteTooltip && emoteTooltip->isVisible() && tooltipMessage == messages[row].seq &&
        tooltipPosition == position && tooltipUrls == info.value(position).second) {
        return true;
    }
    auto item = info.value(position);
    streamPreviewSource.clear();
    tooltipUrls = item.second;
    tooltipMessage = messages[row].seq;
    tooltipPosition = position;
    const bool isBadge = detail.badgeImages.contains(position);
    const bool isChannel = detail.channelImages.contains(position);
    QImage preview;
    if (!tooltipUrls.value(0).startsWith("badge:") && !tooltipUrls.value(0).startsWith("channel:"))
        preview = hub->images->get(tooltipUrls, true);
    if (preview.isNull())
        preview = doc->resource(QTextDocument::ImageResource, QUrl(tooltipUrls.value(0))).value<QImage>();
    const auto host = QUrl(tooltipUrls.value(0)).host();
    const auto source = hub->sources.value(messages[row].source);
    const QString provider = (isBadge || isChannel) && source ? (source->platform == "twitch" ? "Twitch" : source->platform == "kick" ? "Kick" : "YouTube")
                             : host.contains("betterttv") ? "BTTV" : host.contains("7tv") ? "7TV"
                             : host.contains("jtvnw") ? "Twitch" : host.contains("ggpht") ||
                                       host.contains("googleusercontent") ? "YouTube" : QString();
    showImageTooltip(item.first, isChannel ? "Channel" : isBadge ? "Badge" : "Emote", provider, preview, global);
    return true;
}

void ChatView::showChannelTooltip(Source *source, QPoint global) {
    if (!source) { hideEmoteTooltip(); return; }
    streamPreviewSource.clear();
    const QString url = str(source->header.value("avatar"));
    tooltipUrls = url.isEmpty() ? QStringList() : QStringList{url};
    tooltipMessage = 0;
    tooltipPosition = -1;
    QImage preview = url.isEmpty() ? QImage() : hub->images->get({url}, false);
    if (preview.isNull()) preview = channelIcon(source, hub->images, 112);
    showImageTooltip(source->name(), "Channel", source->platform == "twitch" ? "Twitch" : source->platform == "kick" ? "Kick" : "YouTube", preview, global);
}

bool ChatView::showStreamTooltip(Source *source, QPoint global) {
    if (!source || !source->header["live"].toBool()) { hideEmoteTooltip(); return false; }
    streamPreviewSource = source;
    streamPreviewGlobal = global;
    QString url = str(source->header["thumbnail"]);
    if (source->platform == "kick" && (url.isEmpty() || QUrl(url).host() == "stream.kick.com")) {
        source->kickLoadThumbnail();
        url = str(source->header["thumbnail"]);
    }
    if (url.isEmpty() || !Images::allowed(QUrl(url))) {
        if (source->platform == "kick" && source->kickThumbnailPending) {
            tooltipUrls.clear();
            tooltipMessage = 0;
            tooltipPosition = -2;
            showImageTooltip(str(source->header["title"]), "Stream preview", "Kick", {}, global, QSize(320, 180));
            return true;
        }
        hideEmoteTooltip();
        return false;
    }
    if (emoteTooltip && emoteTooltip->isVisible() && tooltipUrls == QStringList{url} && tooltipPosition == -2)
        return true;
    tooltipUrls = {url};
    tooltipMessage = 0;
    tooltipPosition = -2;
    QImage preview = hub->images->get(tooltipUrls, false);
    if (preview.isNull()) preview = hub->images->get(tooltipUrls, false);
    showImageTooltip(str(source->header["title"]), "Stream preview",
                     source->platform == "twitch" ? "Twitch" : source->platform == "kick" ? "Kick" : "YouTube",
                     preview, global, QSize(320, 180));
    return true;
}

void ChatView::showImageTooltip(const QString &name, const QString &kind, const QString &provider,
                                const QImage &image, QPoint global, QSize previewSize) {
    tooltipImageSize = previewSize;
    if (!emoteTooltip) {
        emoteTooltip = new QFrame(this, Qt::ToolTip);
        emoteTooltip->setAttribute(Qt::WA_ShowWithoutActivating);
        emoteTooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
        emoteTooltip->setObjectName("emote-tooltip");
        emoteTooltip->setStyleSheet("QFrame#emote-tooltip{background:#111;border:1px solid #444;}"
                                    "QLabel{color:#efeff1;border:0;background:transparent;}");
        auto *layout = new QVBoxLayout(emoteTooltip);
        layout->setContentsMargins(8, 8, 8, 8);
        emotePreview = new QLabel(emoteTooltip);
        emotePreview->setObjectName("emote-tooltip-preview");
        emoteTooltipName = new QLabel(emoteTooltip);
        emoteTooltipName->setObjectName("emote-tooltip-name");
        emoteTooltipName->setTextFormat(Qt::PlainText);
        emoteTooltipProvider = new QLabel(emoteTooltip);
        emoteTooltipProvider->setObjectName("emote-tooltip-provider");
        layout->addWidget(emotePreview, 0, Qt::AlignCenter);
        layout->addWidget(emoteTooltipName);
        layout->addWidget(emoteTooltipProvider);
    }
    if (image.isNull()) emotePreview->clear();
    else emotePreview->setPixmap(QPixmap::fromImage(image).scaled(previewSize, Qt::KeepAspectRatio,
                                                                  Qt::SmoothTransformation));
    emoteTooltipName->setText(name);
    const QString translatedKind = hub->cfg->T(kind);
    emoteTooltipProvider->setText(provider.isEmpty() ? translatedKind : provider + " · " + translatedKind);
    emoteTooltip->adjustSize();
    auto *screen = QGuiApplication::screenAt(global);
    const QRect available = screen ? screen->availableGeometry() : QRect(global, QSize(400, 400));
    QPoint location = global + QPoint(12, 20);
    location.setX(qBound(available.left(), location.x(), qMax(available.left(), available.right() - emoteTooltip->width())));
    location.setY(qBound(available.top(), location.y(), qMax(available.top(), available.bottom() - emoteTooltip->height())));
    emoteTooltip->move(location);
    emoteTooltip->show();
}

bool ChatView::viewportEvent(QEvent *event) {
    if (event->type() == QEvent::Enter) {
        hoverInside = true;
        updateHoverPause();
    } else if (event->type() == QEvent::Leave) {
        hoverInside = false;
    } else if (event->type() == QEvent::Wheel && hoverSuspended) {
        hoverResumeFollow = false;
    }
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        if (!autoScrolling && showEmoteTooltip(help->pos(), help->globalPos())) {
            return true;
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    if (event->type() == QEvent::Leave || event->type() == QEvent::Wheel ||
        event->type() == QEvent::MouseButtonPress) {
        hideEmoteTooltip();
    }
    if (event->type() == QEvent::Leave) updateHoverPause();
    return QAbstractScrollArea::viewportEvent(event);
}

void ChatView::mousePressEvent(QMouseEvent *e) {
    emit focused();
    setFocus(Qt::MouseFocusReason);
    if (autoScrolling) {
        stopAutoScroll();
        suppressContextMenu = e->button() == Qt::RightButton;
        selectionStart = selectionEnd = {-1, 0};
        e->accept();
        return;
    }
    suppressContextMenu = false;
    if (e->button() == Qt::MiddleButton) {
        if (verticalScrollBar()->maximum() > 0) {
            autoScrollAnchor = e->position().toPoint();
            autoScrollRemainder = 0;
            autoScrolling = true;
            autoScrollClock.start();
            autoScrollTimer.start();
            viewport()->setCursor(Qt::SizeVerCursor);
            viewport()->setAccessibleDescription(
                hub->cfg->T("Autoscroll: move the mouse; click or press Esc to stop."));
            viewport()->update();
        }
        e->accept();
        return;
    }
    if (e->button() == Qt::RightButton && !e->modifiers().testFlag(Qt::ControlModifier)) {
        const int y = e->position().y() + verticalScrollBar()->value();
        const int row = !offsets.isEmpty() && y >= 0 && y < offsets.last() ? rowAt(y) : -1;
        if (row >= 0 && canReply(messages[row])) {
            suppressContextMenu = true;
            emit replyRequested(messages[row]);
            e->accept();
            return;
        }
    }
    if (e->button() != Qt::LeftButton) {
        return;
    }
    pressPoint = e->position().toPoint();
    selectionStart = selectionEnd = positionAt(pressPoint);
    dragging = false;
    viewport()->update();
}

void ChatView::mouseMoveEvent(QMouseEvent *e) {
    if (autoScrolling) {
        hideEmoteTooltip();
        return;
    }
    if (e->buttons().testFlag(Qt::LeftButton) && selectionStart.first >= 0) {
        if (hoverSuspended) hoverResumeFollow = false;
        hideEmoteTooltip();
        dragging |= (e->position().toPoint() - pressPoint).manhattanLength() > 4;
        selectionEnd = positionAt(e->position().toPoint());
        follow = false;
        viewport()->update();
    } else {
        if (e->buttons() != Qt::NoButton) {
            hideEmoteTooltip();
        } else {
            showEmoteTooltip(e->position().toPoint(), e->globalPosition().toPoint());
        }
        auto pos = positionAt(e->position().toPoint());
        bool clickable = false;
        if (pos.first >= 0) {
            auto d = details.value(messages[pos.first].seq);
            clickable = !mentionAt(pos.first, e->position().toPoint()).name.isEmpty() ||
                        (pos.second >= d.nameStart && pos.second < d.nameEnd) ||
                        (!threadMode && !messages[pos.first].reply.isEmpty());
        }
        viewport()->setCursor(clickable ? Qt::PointingHandCursor : Qt::IBeamCursor);
    }
}

void ChatView::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton || selectionStart.first < 0) {
        return;
    }
    auto pos = positionAt(e->position().toPoint());
    if (!dragging && pos.first >= 0) {
        selectionStart = selectionEnd = {-1, 0};
        const auto m = messages[pos.first];
        auto detail = details.value(m.seq);
        if (pos.second >= detail.nameStart && pos.second < detail.nameEnd && !m.system) {
            emit userClicked(m);
        } else if (const auto mention = mentionAt(pos.first, e->position().toPoint()); !mention.name.isEmpty()) {
            Message selected;
            selected.source = m.source;
            selected.name = selected.login = mention.name;
            selected.uid = mention.uid;
            if (auto *source = hub->sources.value(m.source)) {
                for (auto it = source->history.crbegin(); it != source->history.crend(); ++it) {
                    if (!it->system && ((!mention.uid.isEmpty() && it->uid == mention.uid) ||
                        (mention.uid.isEmpty() && (stripAt(it->login) == stripAt(mention.name) ||
                                                   stripAt(it->name) == stripAt(mention.name))))) {
                        selected = *it;
                        break;
                    }
                }
            }
            emit userClicked(selected);
        } else if (!threadMode && !m.reply.isEmpty()) {
            emit threadClicked(m);
        } else {
            QTextCursor c(document(pos.first).data());
            c.setPosition(pos.second);
            QString url = c.charFormat().anchorHref();
            if (!url.isEmpty()) {
                QDesktopServices::openUrl(QUrl(url));
            }
        }
    }
    viewport()->update();
}

void ChatView::contextMenuEvent(QContextMenuEvent *e) {
    if (suppressContextMenu && e->reason() == QContextMenuEvent::Mouse) {
        suppressContextMenu = false;
        e->accept();
        return;
    }
    const int y = e->pos().y() + verticalScrollBar()->value();
    int row =
        e->reason() == QContextMenuEvent::Keyboard || (!offsets.isEmpty() && y >= 0 && y < offsets.last())
            ? rowAt(y)
            : -1;
    if (row >= 0) {
        if (e->reason() == QContextMenuEvent::Mouse && !e->modifiers().testFlag(Qt::ControlModifier) &&
            canReply(messages[row])) {
            emit replyRequested(messages[row]);
        } else {
            emit messageMenuRequested(messages[row], e->globalPos());
        }
    }
    e->accept();
}

bool ChatView::canReply(const Message &message) const {
    auto *source = hub->sources.value(message.source);
    return source && (source->platform == "twitch" || source->platform == "kick") && !message.id.isEmpty() && !message.system &&
           !message.deleted;
}

void ChatView::stopAutoScroll() {
    autoScrollTimer.stop();
    if (!autoScrolling) {
        return;
    }
    autoScrolling = false;
    autoScrollRemainder = 0;
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setAccessibleDescription({});
    QToolTip::hideText();
    viewport()->update();
}

void ChatView::advanceAutoScroll(QPoint pointer, qreal seconds) {
    if (!autoScrolling) {
        return;
    }
    const qreal distance = pointer.y() - autoScrollAnchor.y();
    const qreal outside = qMax<qreal>(0, qAbs(distance) - 8);
    qreal speed = qMin<qreal>(3600, outside * 9 + outside * outside * 0.04);
    if (distance < 0) {
        speed = -speed;
    }
    autoScrollRemainder += speed * qBound<qreal>(0, seconds, 0.1);
    const int delta = int(autoScrollRemainder);
    autoScrollRemainder -= delta;
    if (delta) {
        if (hoverSuspended) hoverResumeFollow = false;
        follow = false;
        verticalScrollBar()->setValue(verticalScrollBar()->value() + delta);
        if (delta > 0 && verticalScrollBar()->value() >= verticalScrollBar()->maximum()) {
            if (hoverSuspended) hoverResumeFollow = true;
            follow = !hoverSuspended;
            latest->hide();
        }
    }
}

void ChatView::focusOutEvent(QFocusEvent *event) {
    stopAutoScroll();
    QAbstractScrollArea::focusOutEvent(event);
}

void ChatView::hideEvent(QHideEvent *event) {
    hoverInside = false;
    hideEmoteTooltip();
    stopAutoScroll();
    QAbstractScrollArea::hideEvent(event);
}

QString ChatView::messageText(int row) {
    auto doc = document(row);
    if (!doc) {
        return {};
    }
    auto text = doc->toPlainText();
    auto images = details.value(messages[row].seq).imageCodes;
    QList<int> positions = images.keys();
    std::sort(positions.begin(), positions.end(), std::greater<int>());
    for (int pos : positions) {
        text.replace(pos, 1, images[pos]);
    }
    return text;
}

QString ChatView::selectedText() {
    auto start = selectionStart, end = selectionEnd;
    if (start > end) {
        std::swap(start, end);
    }
    if (start.first < 0 || end.first >= messages.size() || start == end) {
        return {};
    }
    QStringList rows;
    for (int row = start.first; row <= end.first; ++row) {
        auto doc = document(row);
        int from = row == start.first ? start.second : 0,
            to = row == end.first ? end.second : doc->characterCount() - 1;
        auto text = doc->toPlainText().mid(from, to - from);
        auto images = details.value(messages[row].seq).imageCodes;
        QList<int> positions = images.keys();
        std::sort(positions.begin(), positions.end(), std::greater<int>());
        for (int pos : positions) {
            if (pos >= from && pos < to) {
                text.replace(pos - from, 1, images[pos]);
            }
        }
        rows << text;
    }
    return rows.join('\n');
}

void ChatView::copySelection() {
    auto text = selectedText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void ChatView::keyPressEvent(QKeyEvent *e) {
    if (autoScrolling) {
        stopAutoScroll();
        if (e->key() == Qt::Key_Escape) {
            e->accept();
            return;
        }
    }
    if (e->matches(QKeySequence::Copy)) {
        copySelection();
        return;
    }
    if (e->matches(QKeySequence::SelectAll) && !messages.isEmpty()) {
        selectionStart = {0, 0};
        selectionEnd = {int(messages.size()) - 1, document(messages.size() - 1)->characterCount() - 1};
        follow = false;
        if (hoverSuspended) hoverResumeFollow = false;
        viewport()->update();
        return;
    }
    if (e->key() == Qt::Key_End) {
        jumpLatest();
        return;
    }
    if (e->key() == Qt::Key_Home) {
        follow = false;
        if (hoverSuspended) hoverResumeFollow = false;
        hideEmoteTooltip();
        verticalScrollBar()->setValue(0);
        return;
    }
    QAbstractScrollArea::keyPressEvent(e);
}

void ChatView::wheelEvent(QWheelEvent *e) {
    stopAutoScroll();
    if (hoverSuspended) hoverResumeFollow = false;
    follow = false;
    if (!e->pixelDelta().isNull()) {
        verticalScrollBar()->setValue(verticalScrollBar()->value() - e->pixelDelta().y());
    } else if (e->angleDelta().y()) {
        const qreal multiplier =
            qBound(0.5, hub->cfg->appearance().value("scroll_multiplier").toDouble(2.0), 5.0);
        const qreal line = QFontMetricsF(hub->cfg->messageFont()).lineSpacing();
        wheelRemainder -=
            e->angleDelta().y() / 120.0 * qMax(1, QApplication::wheelScrollLines()) * line * multiplier;
        const int delta = int(wheelRemainder);
        wheelRemainder -= delta;
        verticalScrollBar()->setValue(verticalScrollBar()->value() + delta);
    } else {
        QAbstractScrollArea::wheelEvent(e);
    }
    e->accept();
    if (verticalScrollBar()->value() >= verticalScrollBar()->maximum() - 2) {
        if (hoverSuspended) hoverResumeFollow = true;
        follow = !hoverSuspended;
        latest->hide();
    }
}

void ChatView::jumpLatest() {
    stopAutoScroll();
    hideEmoteTooltip();
    if (hoverSuspended) hoverResumeFollow = true;
    follow = !hoverSuspended;
    selectionStart = selectionEnd = {-1, 0};
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    latest->hide();
    viewport()->update();
}

void ChatView::jumpRow(int row) {
    follow = false;
    if (hoverSuspended) hoverResumeFollow = false;
    hideEmoteTooltip();
    verticalScrollBar()->setValue(offsets.value(row));
    viewport()->update();
}

int ChatView::search(const QString &text, int step) {
    QString old = query;
    query = text;
    if (old != query) {
        documents.clear();
        details.clear();
    }
    QVector<int> found;
    for (int i = 0; i < messages.size(); ++i) {
        if ((messages[i].name + ' ' + messages[i].text + ' ' + str(messages[i].reply["text"]))
                .contains(query, Qt::CaseInsensitive) &&
            !query.isEmpty()) {
            found << i;
        }
    }
    if (!found.isEmpty() && step) {
        int current = rowAt(verticalScrollBar()->value());
        auto it = std::lower_bound(found.begin(), found.end(), current);
        int n = int(it - found.begin()) + (step > 0 ? 1 : -1);
        if (n < 0) {
            n = found.size() - 1;
        }
        if (n >= found.size()) {
            n = 0;
        }
        jumpRow(found[n]);
    } else if (!found.isEmpty() && old != query) {
        jumpRow(found[0]);
    }
    viewport()->update();
    return found.size();
}

Composer::Composer(Images *imageCache, QWidget *parent) : QLineEdit(parent), images(imageCache) {
    suggestionTimer.setSingleShot(true);
    connect(&suggestionTimer, &QTimer::timeout, this, &Composer::refreshSuggestions);
    connect(this, &QLineEdit::textChanged, this, [this] {
        if (!applyingCompletion) {
            hideSuggestions();
            completionValue.clear();
            completions.clear();
            scheduleSuggestions();
        }
    });
    connect(this, &QLineEdit::cursorPositionChanged, this, [this] {
        if (!applyingCompletion) {
            hideSuggestions();
            completionValue.clear();
            completions.clear();
            scheduleSuggestions();
        }
    });
    connect(this, &QLineEdit::selectionChanged, this, &Composer::scheduleSuggestions);
    connect(this, &QLineEdit::textEdited, this, [this] {
        if (!applyingHistory) historyIndex = -1;
    });
    connect(images, &Images::ready, this, [this](const QString &url) {
        if (!suggestionPopup || !suggestionPopup->isVisible()) return;
        for (int row = 0; row < suggestionPopup->count(); ++row) {
            auto *item = suggestionPopup->item(row);
            auto visual = candidateVisuals.value(item->text());
            if (!visual.urls.isEmpty() && visual.urls.first() == url) {
                item->setIcon(suggestionIcon(item->text(), visual));
            }
        }
    });
}

void Composer::recordSent(const QString &text) {
    if (text.trimmed().isEmpty()) return;
    sentHistory << text;
    if (sentHistory.size() > 200) sentHistory.removeFirst();
    historyIndex = -1;
    historyDraft.clear();
}

QString Composer::completionPrefix() const {
    static const QRegularExpression lastWord("\\S+$");
    const auto match = lastWord.match(text().left(cursorPosition()));
    return match.hasMatch() ? match.captured() : QString();
}

void Composer::scheduleSuggestions() {
    if (!applyingCompletion) suggestionTimer.start(0);
}

void Composer::hideSuggestions() {
    suggestionTimer.stop();
    if (suggestionPopup) suggestionPopup->hide();
}

QIcon Composer::suggestionIcon(const QString &value, const CandidateVisual &visual) {
    QImage image;
    if (!visual.urls.isEmpty()) image = images->get(visual.urls, false);
    if (image.isNull() && !visual.urls.isEmpty() && images->cache.contains(visual.urls.first())) {
        image = images->get(visual.urls, false);
    }
    QPixmap icon(24, 24);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    if (visual.user) {
        QPainterPath circle;
        circle.addEllipse(QRectF(1, 1, 22, 22));
        painter.setClipPath(circle);
        if (image.isNull()) {
            painter.fillRect(icon.rect(), QColor("#505059"));
            painter.setPen(Qt::white);
            painter.drawText(icon.rect(), Qt::AlignCenter, value.mid(1, 1).toUpper());
        } else {
            painter.drawImage(icon.rect(), image.scaled(24, 24, Qt::KeepAspectRatioByExpanding,
                                                        Qt::SmoothTransformation));
        }
    } else if (!image.isNull()) {
        const auto scaled = image.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(QPoint((24 - scaled.width()) / 2, (24 - scaled.height()) / 2), scaled);
    }
    return QIcon(icon);
}

void Composer::renderSuggestions(int selected) {
    if (completions.isEmpty() || !hasFocus() || !isVisible()) {
        hideSuggestions();
        return;
    }
    if (!suggestionPopup) {
        suggestionPopup = new QListWidget(this);
        suggestionPopup->setObjectName("completion-suggestions");
        suggestionPopup->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
        suggestionPopup->setAttribute(Qt::WA_ShowWithoutActivating);
        suggestionPopup->setFocusPolicy(Qt::NoFocus);
        suggestionPopup->setSelectionMode(QAbstractItemView::SingleSelection);
        suggestionPopup->setUniformItemSizes(true);
        suggestionPopup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        suggestionPopup->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        suggestionPopup->setIconSize(QSize(24, 24));
        suggestionPopup->setStyleSheet("QListWidget{background:#202024;color:#efeff1;"
                                       "border:1px solid #414148;padding:3px;}"
                                       "QListWidget::item{padding:3px 7px;}"
                                       "QListWidget::item:selected{background:#3a3a40;color:#fff;}");
        connect(suggestionPopup, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            applyCompletion(item->data(Qt::UserRole).toInt());
            setFocus(Qt::OtherFocusReason);
        });
    }
    const int first = selected < 8 ? 0 : selected - 7;
    const int count = qMin(8, int(completions.size()) - first);
    suggestionPopup->clear();
    int textWidth = 130;
    for (int index = first; index < first + count; ++index) {
        const auto value = completions[index];
        auto *item = new QListWidgetItem(suggestionIcon(value, candidateVisuals.value(value)), value,
                                         suggestionPopup);
        item->setData(Qt::UserRole, index);
        item->setSizeHint(QSize(0, 30));
        textWidth = qMax(textWidth, fontMetrics().horizontalAdvance(value));
    }
    suggestionPopup->setCurrentRow(selected < 0 ? 0 : selected - first);
    const int width = qBound(170, textWidth + 60, 360);
    const int height = count * 30 + 8;
    suggestionPopup->resize(width, height);
    const int startX = qMax(0, cursorRect().left() - fontMetrics().horizontalAdvance(completionPrefix()));
    QPoint location = mapToGlobal(QPoint(startX, 0));
    auto *screen = QGuiApplication::screenAt(location);
    const QRect available = screen ? screen->availableGeometry() : QRect(location, QSize(500, 500));
    location.ry() -= height + 3;
    if (location.y() < available.top()) location.setY(mapToGlobal(QPoint(0, this->height() + 3)).y());
    location.setX(qBound(available.left(), location.x(), qMax(available.left(), available.right() - width)));
    suggestionPopup->move(location);
    suggestionPopup->show();
    emit suggestionsShown();
}

void Composer::refreshSuggestions() {
    if (!hasFocus() || !isVisible() || hasSelectedText() || completionPrefix().isEmpty()) {
        hideSuggestions();
        return;
    }
    emit completeRequested();
    const auto prefix = completionPrefix();
    const auto current = text();
    const auto left = current.left(cursorPosition());
    completionHead = left.left(left.size() - prefix.size());
    completionTail = current.mid(cursorPosition());
    const int tailSpace = completionTail.indexOf(QRegularExpression("\\s"));
    completionTail = tailSpace < 0 ? QString() : completionTail.mid(tailSpace);
    completions.clear();
    for (const auto &value : candidates) {
        if (value.startsWith(prefix, Qt::CaseInsensitive)) completions << value;
    }
    completions.removeDuplicates();
    completions.sort(Qt::CaseInsensitive);
    completionIndex = -1;
    navigatedSuggestions = false;
    renderSuggestions();
}

void Composer::applyCompletion(int index) {
    if (index < 0 || index >= completions.size()) return;
    completionIndex = index;
    const QString word = completions[index];
    completionValue = completionHead + word +
                      (completionTail.startsWith(' ') ? QString() : QString(" ")) + completionTail;
    applyingCompletion = true;
    setText(completionValue);
    setCursorPosition(completionHead.size() + word.size() + 1);
    applyingCompletion = false;
    renderSuggestions(index);
}

void Composer::focusOutEvent(QFocusEvent *event) {
    hideSuggestions();
    QLineEdit::focusOutEvent(event);
}

void Composer::hideEvent(QHideEvent *event) {
    hideSuggestions();
    QLineEdit::hideEvent(event);
}

void Composer::keyPressEvent(QKeyEvent *e) {
    if (isReadOnly()) {
        QLineEdit::keyPressEvent(e);
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        if (suggestionPopup && suggestionPopup->isVisible()) {
            hideSuggestions();
            return;
        }
        emit escapePressed();
        return;
    }
    if ((e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) && suggestionPopup &&
        suggestionPopup->isVisible() && suggestionPopup->count()) {
        const int direction = e->key() == Qt::Key_Down ? 1 : -1;
        navigatedSuggestions = true;
        suggestionPopup->setCurrentRow((suggestionPopup->currentRow() + direction + suggestionPopup->count()) %
                                       suggestionPopup->count());
        return;
    }
    if (e->key() == Qt::Key_Up && e->modifiers() == Qt::NoModifier && !sentHistory.isEmpty()) {
        if (historyIndex < 0) {
            historyDraft = text();
            historyIndex = sentHistory.size();
        }
        historyIndex = qMax(0, historyIndex - 1);
        applyingHistory = true;
        setText(sentHistory[historyIndex]);
        applyingHistory = false;
        hideSuggestions();
        return;
    }
    if (e->key() == Qt::Key_Down && e->modifiers() == Qt::NoModifier && historyIndex >= 0) {
        ++historyIndex;
        applyingHistory = true;
        if (historyIndex >= sentHistory.size()) {
            historyIndex = -1;
            setText(historyDraft);
        } else {
            setText(sentHistory[historyIndex]);
        }
        applyingHistory = false;
        hideSuggestions();
        return;
    }
    if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) {
        int direction = e->key() == Qt::Key_Backtab || e->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1;
        QString current = text();
        if (completionValue != current || completions.isEmpty()) {
            const QString selected = suggestionPopup && suggestionPopup->isVisible() && suggestionPopup->currentItem()
                                         ? suggestionPopup->currentItem()->text() : QString();
            emit completeRequested();
            QString left = current.left(cursorPosition());
            auto m = QRegularExpression("\\S+$").match(left);
            if (!m.hasMatch()) {
                return;
            }
            QString prefix = m.captured();
            completionHead = left.left(m.capturedStart());
            completionTail = current.mid(cursorPosition());
            int end = completionTail.indexOf(QRegularExpression("\\s"));
            completionTail = end < 0 ? QString() : completionTail.mid(end);
            completions.clear();
            for (const auto &value : candidates) {
                if (value.startsWith(prefix, Qt::CaseInsensitive)) {
                    completions << value;
                }
            }
            completions.removeDuplicates();
            completions.sort(Qt::CaseInsensitive);
            const int selectedIndex = completions.indexOf(selected);
            completionIndex = selectedIndex >= 0 && (direction > 0 || navigatedSuggestions)
                                  ? selectedIndex : direction > 0 ? 0 : completions.size() - 1;
        } else {
            completionIndex = (completionIndex + direction + completions.size()) % completions.size();
        }
        if (completions.isEmpty()) {
            return;
        }
        applyCompletion(completionIndex);
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) hideSuggestions();
    completionValue.clear();
    QLineEdit::keyPressEvent(e);
}

bool Composer::event(QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            keyPressEvent(key);
            return true;
        }
    }
    return QLineEdit::event(event);
}

Pane::Pane(Hub *h, QList<Source *> list, QWidget *p, bool isDetached)
    : QWidget(p), hub(h), uid(QUuid::createUuid().toString(QUuid::WithoutBraces)), detached(isDetached) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    header = new QWidget(this);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(6, 3, 4, 3);
    headerIcon = new QLabel(header);
    headerIcon->setObjectName("channel-header-icon");
    headerIcon->setFixedSize(20, 20);
    headerIcon->setMouseTracking(true);
    headerIcon->installEventFilter(this);
    hl->addWidget(headerIcon);
    headerText = new QLabel(header);
    headerText->setObjectName("channel-header-title");
    headerText->setMouseTracking(true);
    headerText->installEventFilter(this);
    headerText->setWordWrap(true);
    headerText->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    headerText->setTextFormat(Qt::PlainText);
    headerText->setFont(QFont("Segoe UI", 9, QFont::Bold));
    hl->addWidget(headerText, 1);
    mixedHeader = new QWidget(header);
    mixedHeader->setObjectName("mixed-channel-header");
    auto *mixedLayout = new QVBoxLayout(mixedHeader);
    mixedLayout->setContentsMargins(0, 0, 0, 0);
    mixedLayout->setSpacing(2);
    for (int i = 0; i < 3; ++i) {
        auto *row = new QWidget(mixedHeader);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(5);
        auto *icon = new QLabel(row);
        icon->setObjectName("mixed-channel-icon");
        icon->setFixedSize(20, 20);
        icon->setMouseTracking(true);
        icon->installEventFilter(this);
        auto *title = new QLabel(row);
        title->setObjectName("mixed-channel-title");
        title->setTextFormat(Qt::PlainText);
        title->setWordWrap(true);
        title->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        title->setFont(QFont("Segoe UI", 9, QFont::Bold));
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        title->setMouseTracking(true);
        title->installEventFilter(this);
        rowLayout->addWidget(icon);
        rowLayout->addWidget(title, 1);
        mixedLayout->addWidget(row);
        mixedIcons << icon;
        mixedTitles << title;
    }
    mixedMore = new QLabel(mixedHeader);
    mixedMore->setObjectName("mixed-channel-more");
    mixedLayout->addWidget(mixedMore);
    hl->addWidget(mixedHeader, 1);
    mixedHeader->hide();
    auto *statusStrip = new QWidget(this);
    statusStrip->setObjectName("chat-status-strip");
    auto *statusLayout = new QHBoxLayout(statusStrip);
    statusLayout->setContentsMargins(8, 3, 6, 3);
    statusLayout->setSpacing(6);
    statusStrip->setStyleSheet("QWidget#chat-status-strip{background:#111111;}");
    filterButton = new QToolButton(statusStrip);
    filterButton->setObjectName("active-filter");
    filterButton->setStyleSheet("QToolButton{color:#f0c674;background:#29251c;border-radius:3px;padding:3px 5px;}"
                                "QToolButton:hover{background:#443a27;}");
    filterButton->hide();
    connect(filterButton, &QToolButton::clicked, this, [this] { emit filtersRequested(this); });
    phoneButton = new QToolButton(statusStrip);
    phoneButton->setObjectName("active-phone");
    phoneButton->setStyleSheet("QToolButton{color:#b5e4c3;background:#183123;border-radius:3px;padding:3px 5px;}"
                               "QToolButton:hover{background:#245036;}");
    phoneButton->hide();
    connect(phoneButton, &QToolButton::clicked, this, [this] { emit phoneRequested(this); });
    auto *actions = new QToolButton(header);
    actions->setText("▾");
    hl->addWidget(actions);
    layout->addWidget(header);
    headerResize = new QWidget(this);
    headerResize->setObjectName("header-resize");
    headerResize->setFixedHeight(5);
    headerResize->setCursor(Qt::SizeVerCursor);
    headerResize->setStyleSheet("background:#111111;border-bottom:1px solid #282828;");
    headerResize->installEventFilter(this);
    layout->addWidget(headerResize);
    chatModesLabel = new QLabel(statusStrip);
    chatModesLabel->setObjectName("chat-modes");
    chatModesLabel->setTextFormat(Qt::PlainText);
    chatModesLabel->setWordWrap(true);
    chatModesLabel->setStyleSheet("background:transparent;color:#b7b7b7;font-size:11px;");
    statusLayout->addWidget(chatModesLabel, 1);
    statusLayout->addWidget(filterButton);
    statusLayout->addWidget(phoneButton);
    layout->addWidget(statusStrip);
    decorations = new QWidget(this);
    decorations->setObjectName("chat-decorations");
    auto *decorationLayout = new QVBoxLayout(decorations);
    decorationLayout->setContentsMargins(4, 2, 4, 2);
    decorationLayout->setSpacing(2);
    layout->addWidget(decorations);
    decorations->hide();
    header->setStyleSheet("background:#111111;");
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(actions, &QToolButton::clicked, this, [this, actions] {
        emit menuRequested(this, actions->mapToGlobal(QPoint(0, actions->height())));
    });
    connect(header, &QWidget::customContextMenuRequested, this,
            [this](QPoint p) { emit menuRequested(this, header->mapToGlobal(p)); });
    searchBar = new QWidget(this);
    auto *searchLayout = new QHBoxLayout(searchBar);
    searchLayout->setContentsMargins(4, 2, 4, 2);
    searchInput = new QLineEdit(searchBar);
    searchInput->setPlaceholderText(hub->cfg->T("Search messages…"));
    searchInput->setClearButtonEnabled(true);
    searchCount = new QLabel(searchBar);
    auto *previous = new QToolButton(searchBar);
    previous->setText("↑");
    auto *next = new QToolButton(searchBar);
    next->setText("↓");
    auto *closeSearch = new QToolButton(searchBar);
    closeSearch->setText("×");
    searchLayout->addWidget(searchInput, 1);
    searchLayout->addWidget(searchCount);
    searchLayout->addWidget(previous);
    searchLayout->addWidget(next);
    searchLayout->addWidget(closeSearch);
    layout->addWidget(searchBar);
    searchBar->hide();
    view = new ChatView(hub, this);
    layout->addWidget(view, 1);
    connect(hub->images, &Images::ready, this, [this](const QString &url) {
        for (const auto &source : sources) {
            if (source && str(source->header.value("avatar")) == url) { refresh(); break; }
        }
    });
    replyBar = new QWidget(this);
    auto *rl = new QHBoxLayout(replyBar);
    rl->setContentsMargins(5, 2, 4, 2);
    replyLabel = new QLabel(replyBar);
    replyLabel->setTextFormat(Qt::PlainText);
    replyLabel->setWordWrap(true);
    rl->addWidget(replyLabel, 1);
    auto *cancel = new QToolButton(replyBar);
    cancel->setText("×");
    rl->addWidget(cancel);
    layout->addWidget(replyBar);
    replyBar->hide();
    connect(cancel, &QToolButton::clicked, this, &Pane::cancelReply);
    auto *composer = new QWidget(this);
    composer->setStyleSheet("background:#111111;");
    auto *cl = new QHBoxLayout(composer);
    cl->setContentsMargins(4, 3, 4, 3);
    cl->setSpacing(3);
    target = new QComboBox(composer);
    target->setMaximumWidth(140);
    input = new Composer(hub->images, composer);
    input->installEventFilter(this);
    header->installEventFilter(this);
    headerText->installEventFilter(this);
    input->setFrame(false);
    input->setPlaceholderText(hub->cfg->T("Send a message…"));
    countLabel = new QLabel(composer);
    countLabel->setStyleSheet("color:#8b8b94;font-size:10px;");
    emoteButton = new QPushButton("☺", composer);
    emoteButton->setMaximumWidth(28);
    sendButton = new QPushButton(hub->cfg->T("Send"), composer);
    cl->addWidget(target);
    cl->addWidget(input, 1);
    cl->addWidget(countLabel);
    cl->addWidget(emoteButton);
    cl->addWidget(sendButton);
    layout->addWidget(composer);
    connect(input, &QLineEdit::returnPressed, this, &Pane::send);
    connect(sendButton, &QPushButton::clicked, this, &Pane::send);
    connect(emoteButton, &QPushButton::clicked, this, &Pane::openEmotes);
    connect(input, &Composer::escapePressed, this, &Pane::cancelReply);
    auto updateCount = [this](QString text) {
        const QString platform = targetSource() ? targetSource()->platform : QString();
        int limit = platform == "youtube" ? 200 : 500;
        int length = platformCharacterCount(text, platform);
        countLabel->setText(text.isEmpty() ? QString()
                                           : QString("%1/%2").arg(length).arg(limit));
    };
    connect(input, &QLineEdit::textChanged, this, updateCount);
    connect(input, &Composer::completeRequested, this, [this] {
        QStringList c;
        input->candidateVisuals.clear();
        auto s = targetSource();
        if (s) {
            const auto prefix = input->completionPrefix();
            if (prefix.startsWith('@')) {
                QSet<QString> seen;
                for (auto it = s->history.crbegin(); it != s->history.crend(); ++it) {
                    if (it->system) continue;
                    QString name = it->login.isEmpty() ? it->name : it->login;
                    if (it->name.compare(name, Qt::CaseInsensitive) == 0) name = it->name;
                    name = name.trimmed();
                    while (name.startsWith('@')) name.remove(0, 1);
                    const auto value = '@' + name;
                    const auto key = value.toCaseFolded();
                    if (!name.isEmpty() && value.startsWith(prefix, Qt::CaseInsensitive) && !seen.contains(key)) {
                        seen.insert(key);
                        c << value;
                        QStringList urls;
                        if (!it->avatar.isEmpty()) urls << it->avatar;
                        if (urls.isEmpty() && s->platform == "twitch") {
                            const auto avatar = hub->twitchAvatarUrls.value(name.toLower());
                            if (!avatar.isEmpty()) urls << avatar;
                        }
                        input->candidateVisuals[value] = {urls, true};
                    }
                }
            } else if (!prefix.isEmpty()) {
                for (auto it = s->emotes.cbegin(); it != s->emotes.cend(); ++it) {
                    if (it.key().startsWith(prefix, Qt::CaseInsensitive)) {
                        c << it.key();
                        input->candidateVisuals[it.key()] = {it->urls, false};
                    }
                }
            }
        }
        input->candidates = c;
    });
    auto *avatarTimer = new QTimer(this);
    avatarTimer->setSingleShot(true);
    avatarTimer->setInterval(350);
    connect(input, &Composer::suggestionsShown, this, [this, avatarTimer] {
        auto *s = targetSource();
        if (s && s->platform == "twitch" && input->completionPrefix().startsWith('@')) avatarTimer->start();
    });
    connect(avatarTimer, &QTimer::timeout, this, [this] {
        auto *s = targetSource();
        auto *popup = input->suggestionPopup;
        if (!s || s->platform != "twitch" || !popup || !popup->isVisible() ||
            !input->completionPrefix().startsWith('@')) return;
        QStringList missing;
        for (int row = 0; row < popup->count(); ++row) {
            const auto name = popup->item(row)->text();
            if (input->candidateVisuals.value(name).urls.isEmpty()) missing << name;
        }
        hub->requestTwitchAvatars(missing);
    });
    connect(hub, &Hub::twitchAvatarsReady, this, [this] {
        auto *popup = input->suggestionPopup;
        auto *s = targetSource();
        if (!s || s->platform != "twitch" || !popup || !popup->isVisible()) return;
        for (int row = 0; row < popup->count(); ++row) {
            auto *item = popup->item(row);
            const auto avatar = hub->twitchAvatarUrls.value(stripAt(item->text()).toLower());
            if (!avatar.isEmpty()) {
                auto visual = input->candidateVisuals.value(item->text());
                visual.urls = {avatar};
                visual.user = true;
                input->candidateVisuals[item->text()] = visual;
                item->setIcon(input->suggestionIcon(item->text(), visual));
            }
        }
    });
    connect(target, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, updateCount] {
        updateCount(input->text());
        input->hideSuggestions();
        input->scheduleSuggestions();
        if (!detached) {
            cancelReply();
        }
        auto s = targetSource();
        sendButton->setText(hub->cfg->T(s && s->platform == "youtube" ? "Send · 20" : "Send"));
    });
    connect(view, &ChatView::focused, this, [this] { emit focused(this); });
    connect(view, &ChatView::userClicked, this,
            [this](Message m) { emit userRequested(this, hub->sources.value(m.source), m); });
    connect(view, &ChatView::threadClicked, this,
            [this](Message m) { emit threadRequested(hub->sources.value(m.source), m); });
    connect(view, &ChatView::messageMenuRequested, this,
            [this](Message m, QPoint p) { emit messageMenu(this, m, p); });
    connect(view, &ChatView::replyRequested, this, &Pane::startReply);
    connect(searchInput, &QLineEdit::textChanged, this,
            [this](const QString &s) { searchCount->setText(QString::number(view->search(s))); });
    connect(previous, &QToolButton::clicked, this, [this] { view->search(searchInput->text(), -1); });
    connect(next, &QToolButton::clicked, this, [this] { view->search(searchInput->text(), 1); });
    connect(closeSearch, &QToolButton::clicked, this, [this] {
        searchBar->hide();
        searchInput->clear();
        view->setFocus();
    });
    connect(hub->cfg, &Config::appearanceChanged, this, [this] {
        filter = hub->cfg->data["chat_filters"].toObject()[key()].toObject();
        view->messageCap = hub->cfg->cap(key());
        for (auto s : sources) {
            if (s) {
                s->viewCaps[reinterpret_cast<quintptr>(this)] = view->messageCap;
                s->trim();
            }
        }
        reload();
        refresh();
    });
    connect(hub->cfg, &Config::filtersChanged, this, [this](const QString &changedKey) {
        if (changedKey != key()) return;
        filter = hub->cfg->data["chat_filters"].toObject()[key()].toObject();
        reload();
        refresh();
    });
    connect(hub->cfg, &Config::languageChanged, this, [this] { refresh(); refreshDecorations(); refreshChatModes(); });
    historyReload.setSingleShot(true);
    connect(&historyReload, &QTimer::timeout, this, &Pane::reload);
    headerClock.setInterval(1000);
    connect(&headerClock, &QTimer::timeout, this, [this] {
        if (!isVisible() || !hub->cfg->appearance()["header_duration"].toBool(true)) return;
        for (auto source : sources) {
            if (source && source->header["live"].toBool() && !str(source->header["started_at"]).isEmpty()) {
                refresh();
                break;
            }
        }
    });
    headerClock.start();
    setSources(list);
    if (detached) {
        header->hide();
        headerResize->hide();
        statusStrip->hide();
        chatModesLabel->hide();
        target->hide();
        view->threadMode = true;
    }
}

Source *Pane::targetSource() const {
    int i = target->currentIndex();
    return i >= 0 && i < sources.size() ? sources[i].data() : nullptr;
}

Pane::~Pane() {
    for (auto *icon : mixedIcons) icon->removeEventFilter(this);
    for (auto s : sources) {
        if (s) {
            s->viewCaps.remove(reinterpret_cast<quintptr>(this));
        }
    }
}

QString Pane::key() const {
    return sources.size() == 1 && sources[0] ? sources[0]->key : "mix:" + uid;
}

void Pane::setSources(QList<Source *> list) {
    if (sending) {
        ++sendEpoch;
        sending = false;
        input->setReadOnly(false);
        sendButton->setEnabled(true);
    }
    for (auto source : sources) {
        if (source) {
            disconnect(source, nullptr, this, nullptr);
            source->viewCaps.remove(reinterpret_cast<quintptr>(this));
        }
    }
    sources.clear();
    target->clear();
    for (auto *s : list) {
        if (!s || sources.contains(s)) {
            continue;
        }
        sources << s;
        s->viewCaps[reinterpret_cast<quintptr>(this)] = hub->cfg->cap(key());
        target->addItem((s->platform == "twitch" ? "TW · " : s->platform == "kick" ? "K · " : "YT · ") + s->name(), s->key);
        connect(s, &Source::message, this, [this, s](Message m) { incoming(s, m); });
        connect(s, &Source::changed, this, [this] {
            refresh();
            refreshChatModes();
            reload();
        });
        connect(s, &Source::headerChanged, this, [this, s] {
            refresh();
            refreshChatModes();
            if (view->streamPreviewSource == s && view->emoteTooltip && view->emoteTooltip->isVisible())
                view->showStreamTooltip(s, view->streamPreviewGlobal);
        });
        connect(s, &Source::decorationsChanged, this, &Pane::refreshDecorations);
        connect(s, &Source::chatModesChanged, this, &Pane::refreshChatModes);
        connect(s, &Source::redacted, this, [this](QJsonObject target) {
            if (detached || reply.id.isEmpty()) {
                return;
            }
            if (target["all"].toBool() || str(target["message_id"]) == reply.id ||
                (!str(target["user_id"]).isEmpty() && str(target["user_id"]) == reply.uid) ||
                (!str(target["user_login"]).isEmpty() && stripAt(str(target["user_login"])) == stripAt(reply.login))) {
                cancelReply();
            }
        });
    }
    target->setVisible(!detached && sources.size() > 1);
    filter = hub->cfg->data["chat_filters"].toObject()[key()].toObject();
    view->messageCap = hub->cfg->cap(key());
    view->mixed = sources.size() > 1;
    for (auto s : sources) {
        if (s) {
            s->viewCaps[reinterpret_cast<quintptr>(this)] = view->messageCap;
        }
    }
    reload();
    refresh();
    refreshDecorations();
    refreshChatModes();
    emit sourcesChanged();
}

void Pane::refreshChatModes() {
    QStringList labels;
    for (auto source : sources) {
        if (!source || source->closed) continue;
        const auto label = source->chatModeSummary();
        labels << (sources.size() > 1 ? source->name() + ": " + label : label);
    }
    chatModesLabel->setText(labels.join("\n"));
    chatModesLabel->setVisible(!detached && !labels.isEmpty());
}

void Pane::refreshDecorations() {
    auto *box = qobject_cast<QVBoxLayout *>(decorations->layout());
    while (auto *item = box->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    int count = 0;
    for (auto source : sources) {
        if (!source) continue;
        const auto add = [&](const QString &heading, const QJsonObject &value) {
            if (str(value["text"]).isEmpty()) return;
            QString text = heading;
            if (sources.size() > 1) text += " · " + source->name();
            const QString author = str(value["name"]);
            text += "\n" + (author.isEmpty() ? QString() : author + ": ") + str(value["text"]);
            if (!str(value["options"]).isEmpty()) text += "\n" + str(value["options"]);
            auto *label = new QLabel(text, decorations);
            label->setObjectName("chat-decoration");
            label->setTextFormat(Qt::PlainText);
            label->setWordWrap(true);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            label->setStyleSheet("background:#1c1c20; color:#e9e9ec; border-left:2px solid #9147ff; padding:4px;");
            box->addWidget(label);
            ++count;
        };
        add(hub->cfg->T("Pinned message"), source->pinned);
        QString pollHeading = hub->cfg->T(source->activePoll.value("ended").toBool() ? "Poll result" : "Poll");
        if (source->activePoll.value("last_observed").toBool()) pollHeading += " · " + hub->cfg->T("last observed");
        add(pollHeading, source->activePoll);
        QString predictionHeading = hub->cfg->T(source->activePrediction.value("ended").toBool() ? "Prediction result" : "Prediction");
        if (str(source->activePrediction.value("status")) == "LOCKED") predictionHeading += " · " + hub->cfg->T("Locked");
        if (str(source->activePrediction.value("status")) == "CANCELED") predictionHeading += " · " + hub->cfg->T("Canceled");
        add(predictionHeading, source->activePrediction);
    }
    decorations->setVisible(count > 0 && !detached);
}

void Pane::refresh() {
    const bool hasFilter = !filter["hidden_users"].toArray().isEmpty() ||
        !filter["include_terms"].toArray().isEmpty() || !filter["exclude_terms"].toArray().isEmpty() ||
        filter["hide_deleted"].toBool() || filter["hide_system"].toBool();
    phoneButton->setText(hub->cfg->T("Phone"));
    phoneButton->setToolTip(hub->cfg->T("Phone access is active. Click to open or stop it."));
    filterButton->setText(hub->cfg->T("Filters"));
    filterButton->setToolTip(hub->cfg->T("Filters are active. Click to review them."));
    filterButton->setVisible(hasFilter && !detached);
    input->setPlaceholderText(hub->cfg->T("Send a message…"));
    sendButton->setText(hub->cfg->T("Send"));
    searchInput->setPlaceholderText(hub->cfg->T("Search messages…"));
    view->latest->setText(hub->cfg->T("↓ Latest"));
    if (!reply.id.isEmpty()) {
        replyLabel->setText(hub->cfg->T(
            "Replying to @{name}: {body}",
            {{"name", reply.login.isEmpty() ? reply.name : reply.login}, {"body", reply.text.left(120)}}));
    }
    QStringList titles, fullTitles;
    const auto appearance = hub->cfg->appearance();
    for (auto source : sources) {
        if (source) {
            QString count;
            if (source->header.contains("live") && !source->header["live"].toBool()) {
                count = hub->cfg->T("Offline");
            } else if (source->header.contains("viewers") && source->header["viewers"].toDouble() >= 0) {
                count = hub->cfg->number(qint64(source->header["viewers"].toDouble()));
            } else {
                count = "—";
            }
            QString title = str(source->header["title"]);
            if (title.isEmpty()) {
                title = source->name();
            }
            QStringList parts;
            if (appearance["header_viewers"].toBool(true)) parts << count;
            if (appearance["header_duration"].toBool(true) && source->header["live"].toBool()) {
                const auto start = QDateTime::fromString(str(source->header["started_at"]), Qt::ISODate);
                const qint64 seconds = start.isValid() ? start.secsTo(QDateTime::currentDateTimeUtc()) : -1;
                if (seconds >= 0) {
                    const qint64 hours = seconds / 3600;
                    parts << QString("%1:%2:%3").arg(hours, 2, 10, QChar('0'))
                                 .arg((seconds / 60) % 60, 2, 10, QChar('0'))
                                 .arg(seconds % 60, 2, 10, QChar('0'));
                }
            }
            if (appearance["header_category"].toBool(true) && source->platform != "youtube" &&
                !str(source->header["category"]).isEmpty()) parts << str(source->header["category"]);
            if (appearance["header_title"].toBool(true)) parts << title;
            const QString text = parts.join(" - ");
            titles << text;
            fullTitles << source->name() + (text.isEmpty() ? QString() : " — " + text);
        }
    }
    const bool isMixed = sources.size() > 1;
    headerText->setVisible(!isMixed);
    headerIcon->setVisible(!isMixed);
    mixedHeader->setVisible(isMixed);
    headerText->setText(titles.value(0));
    mixedHeader->setToolTip(fullTitles.join('\n'));
    auto *mixedLayout = qobject_cast<QVBoxLayout *>(mixedHeader->layout());
    while (mixedIcons.size() < sources.size()) {
        auto *row = new QWidget(mixedHeader);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(5);
        auto *icon = new QLabel(row);
        icon->setObjectName("mixed-channel-icon");
        icon->setFixedSize(20, 20);
        icon->setMouseTracking(true);
        icon->installEventFilter(this);
        auto *title = new QLabel(row);
        title->setObjectName("mixed-channel-title");
        title->setTextFormat(Qt::PlainText);
        title->setWordWrap(true);
        title->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        title->setFont(QFont("Segoe UI", 9, QFont::Bold));
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        title->setMouseTracking(true);
        title->installEventFilter(this);
        rowLayout->addWidget(icon);
        rowLayout->addWidget(title, 1);
        mixedLayout->insertWidget(mixedLayout->count() - 1, row);
        mixedIcons << icon;
        mixedTitles << title;
    }
    for (int i = 0; i < mixedIcons.size(); ++i) {
        auto *icon = mixedIcons[i];
        auto *title = mixedTitles[i];
        auto *source = i < sources.size() ? sources[i].data() : nullptr;
        icon->parentWidget()->setVisible(isMixed && source);
        if (!source) continue;
        icon->setProperty("source-key", source->key);
        icon->setPixmap(QPixmap::fromImage(channelIcon(source, hub->images, 20)));
        icon->setAccessibleName(source->name());
        title->setText(titles.value(i));
        title->setProperty("source-key", source->key);
        title->setToolTip(fullTitles.value(i));
    }
    mixedMore->hide();
    applyHeaderLineLimit();
    if (isMixed) header->setMinimumHeight(0);
    if (sources.size() == 1 && sources[0]) {
        headerText->setProperty("source-key", sources[0]->key);
        headerText->setToolTip(fullTitles.value(0));
        headerIcon->setProperty("source-key", sources[0]->key);
        headerIcon->setAccessibleName(sources[0]->name());
        headerIcon->setPixmap(QPixmap::fromImage(channelIcon(sources[0], hub->images, 20)));
    }
    int selected = target->currentIndex();
    for (int i = 0; i < sources.size(); ++i) {
        if (sources[i]) {
            target->setItemText(i,
                                (sources[i]->platform == "twitch" ? "TW · " : sources[i]->platform == "kick" ? "K · " : "YT · ") + sources[i]->name());
        }
    }
    if (selected >= 0) {
        target->setCurrentIndex(selected);
    }
}

void Pane::reload() {
    if (detached && !threadId.isEmpty()) {
        return;
    }
    QVector<Message> all;
    for (auto s : sources) {
        if (s) {
            for (const auto &m : s->history) {
                if (accepts(m, filter, hub->cfg) && (!extraFilter || extraFilter(m))) {
                    all << m;
                }
            }
        }
    }
    std::stable_sort(all.begin(), all.end(), messageOrder);
    view->setMessages(all);
}

void Pane::incoming(Source *s, Message m) {
    Q_UNUSED(s)
    if (detached && !threadId.isEmpty()) {
        return;
    }
    if (accepts(m, filter, hub->cfg) && (!extraFilter || extraFilter(m))) {
        if (m.history) historyReload.start(0);
        else view->append(m);
    }
}

void Pane::send() {
    auto *s = targetSource();
    if (!s || sending || input->text().trimmed().isEmpty()) {
        return;
    }
    QString text = input->text();
    sending = true;
    const auto epoch = ++sendEpoch;
    input->hideSuggestions();
    input->setReadOnly(true);
    sendButton->setEnabled(false);
    QPointer<Pane> self(this);
    hub->send(s, text, reply, [self, text, epoch](QJsonObject, QString error) {
        if (!self || self->sendEpoch != epoch) {
            return;
        }
        self->sending = false;
        self->input->setReadOnly(false);
        self->sendButton->setEnabled(true);
        if (!error.isEmpty()) {
            QMessageBox::warning(self, self->hub->cfg->T("Send message"), self->hub->cfg->T(error));
            return;
        }
        self->input->recordSent(text);
        if (self->input->text() == text) {
            self->input->clear();
        }
        if (!self->detached) {
            self->cancelReply();
        }
    });
}

void Pane::startReply(Message m) {
    auto s = hub->sources.value(m.source);
    if (!s || (s->platform != "twitch" && s->platform != "kick") || m.id.isEmpty() || m.deleted) {
        return;
    }
    target->setCurrentIndex(sources.indexOf(s));
    reply = m;
    replyLabel->setText(
        hub->cfg->T("Replying to @{name}: {body}",
                    {{"name", m.login.isEmpty() ? m.name : m.login}, {"body", m.text.left(120)}}));
    replyBar->setVisible(!detached);
    input->setFocus();
}

void Pane::cancelReply() {
    if (!detached) {
        reply = Message();
    }
    replyBar->hide();
}

void Pane::showSearch() {
    searchBar->show();
    searchInput->setFocus();
    searchInput->selectAll();
}

bool Pane::eventFilter(QObject *object, QEvent *event) {
    const bool titleDragTarget = object == headerResize || object == header || object == headerText ||
                                 mixedTitles.contains(qobject_cast<QLabel *>(object));
    if (titleDragTarget && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            // Start at the visible height, not a saved limit that might be far
            // larger than the current title. In a mixed pane use the tallest
            // visible title because the limit applies to every channel.
            const auto visibleLines = [](QLabel *label) {
                const int lineHeight = qMax(1, QFontMetrics(label->font()).lineSpacing());
                const int naturalHeight = label->width() > 0 ? label->heightForWidth(label->width()) : -1;
                const int usedHeight = naturalHeight > 0 ? qMin(label->height(), naturalHeight) : label->height();
                return qMax(1, qRound(double(usedHeight) / lineHeight));
            };
            headerDragStartLines = 1;
            if (sources.size() > 1) {
                for (int i = 0; i < sources.size() && i < mixedTitles.size(); ++i)
                    if (sources[i]) headerDragStartLines = qMax(headerDragStartLines, visibleLines(mixedTitles[i]));
            } else {
                headerDragStartLines = visibleLines(headerText);
            }
            headerDragStartY = mouse->globalPosition().toPoint().y();
            headerDragging = true;
            headerDragMoved = false;
            headerDragWidget = qobject_cast<QWidget *>(object);
            if (headerDragWidget && QGuiApplication::platformName() != "offscreen") headerDragWidget->grabMouse();
            return true;
        }
    } else if (headerDragging && event->type() == QEvent::MouseMove && titleDragTarget) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->buttons().testFlag(Qt::LeftButton)) {
            const int lineHeight = qMax(1, QFontMetrics(headerText->font()).lineSpacing());
            const int delta = mouse->globalPosition().toPoint().y() - headerDragStartY;
            if (qAbs(delta) >= 3) {
                const int lines = qBound(1, headerDragStartLines + qRound(double(delta) / lineHeight), 100);
                if (lines != headerLines) { headerLines = lines; headerDragMoved = true; applyHeaderLineLimit(); }
                if (view) view->hideEmoteTooltip();
                return true;
            }
        }
    } else if (headerDragging && event->type() == QEvent::MouseButtonRelease && titleDragTarget) {
        headerDragging = false;
        if (headerDragWidget && QWidget::mouseGrabber() == headerDragWidget) headerDragWidget->releaseMouse();
        headerDragWidget.clear();
        if (headerDragMoved) emit headerLinesChanged();
        return true;
    }
    if (auto *icon = qobject_cast<QLabel *>(object);
        view && icon && (icon == headerIcon || mixedIcons.contains(icon))) {
        auto *source = hub->sources.value(icon->property("source-key").toString());
        if (event->type() == QEvent::MouseMove) {
            view->showChannelTooltip(source, static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
        } else if (event->type() == QEvent::ToolTip) {
            view->showChannelTooltip(source, static_cast<QHelpEvent *>(event)->globalPos());
            return true;
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) {
            view->hideEmoteTooltip();
        }
    }
    if (auto *title = qobject_cast<QLabel *>(object);
        view && title && (title == headerText || mixedTitles.contains(title))) {
        auto *source = hub->sources.value(title->property("source-key").toString());
        if (event->type() == QEvent::MouseMove) {
            view->showStreamTooltip(source, static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
        } else if (event->type() == QEvent::ToolTip) {
            if (view->showStreamTooltip(source, static_cast<QHelpEvent *>(event)->globalPos())) return true;
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) {
            view->hideEmoteTooltip();
        }
    }
    if ((object == header || object == headerText || mixedTitles.contains(qobject_cast<QLabel *>(object))) &&
        event->type() == QEvent::MouseButtonDblClick) {
        for (QWidget *p = parentWidget(); p; p = p->parentWidget()) {
            if (auto *w = qobject_cast<Workspace *>(p)) {
                w->toggleZoom(this);
                return true;
            }
        }
    }
    Q_UNUSED(object)
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress) {
        emit focused(this);
    }
    return QWidget::eventFilter(object, event);
}

void Pane::applyHeaderLineLimit() {
    bool changed = false;
    auto apply = [this, &changed](QLabel *label) {
        if (!label) return;
        const int height = headerLines > 0 ? qMax(1, QFontMetrics(label->font()).lineSpacing() * headerLines) : QWIDGETSIZE_MAX;
        if (label->maximumHeight() == height) return;
        changed = true;
        label->setMaximumHeight(height);
        label->updateGeometry();
    };
    apply(headerText);
    for (auto *label : mixedTitles) apply(label);
    if (!changed) return;
    header->setMinimumHeight(0);
    header->updateGeometry();
    updateGeometry();
}

void Pane::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (!detached) {
        QTimer::singleShot(0, this, [this] {
            if (sources.size() <= 1 && headerText->width() > 50) {
                int h = qMax(24, qMin(headerText->heightForWidth(headerText->width()), headerText->maximumHeight()) + 6);
                if (header->minimumHeight() != h) {
                    header->setMinimumHeight(h);
                }
            }
        });
    }
}

QJsonObject Pane::serialize() const {
    QJsonArray keys;
    for (auto s : sources) {
        if (s) {
            keys.append(s->key);
        }
    }
    return {{"sources", keys}, {"pane_id", uid}, {"header_lines", headerLines}};
}

struct UnlockedEmotes {
    QHash<QString, Span> items;
    QString user, error;
    bool loading = false;
    quint64 generation = 0;
};

static void loadUnlockedEmotes(Accounts *accounts, QPointer<QDialog> dialog,
                               QSharedPointer<UnlockedEmotes> catalog, QString broadcaster,
                               std::function<void()> done, quint64 generation, QString cursor = {},
                               int page = 0) {
    if (!dialog || generation != catalog->generation) {
        return;
    }
    QMap<QString, QString> params{{"user_id", catalog->user}};
    if (!broadcaster.isEmpty()) {
        params["broadcaster_id"] = broadcaster;
    }
    if (!cursor.isEmpty()) {
        params["after"] = cursor;
    }
    accounts->helix(
        "chat/emotes/user",
        [=](QJsonObject data, QString error) {
            if (!dialog || generation != catalog->generation) {
                return;
            }
            if (!error.isEmpty()) {
                catalog->error = error;
                catalog->loading = false;
                done();
                return;
            }
            for (const auto &value : data.value("data").toArray()) {
                const auto item = value.toObject();
                const auto type = str(item.value("emote_type"));
                if (type == "globals" || type == "smilies") {
                    continue;
                }
                const auto id = str(item.value("id"));
                Span emote;
                emote.code = emoteName(str(item.value("name")));
                emote.urls = {"https://static-cdn.jtvnw.net/emoticons/v2/" + id + "/default/dark/3.0"};
                if (!id.isEmpty() && !emote.code.isEmpty()) {
                    catalog->items[emote.code] = emote;
                }
            }
            const auto next = str(at(data, {"pagination", "cursor"}));
            const bool more = !next.isEmpty() && next != cursor && page < 49;
            catalog->loading = more;
            done();
            if (more) {
                loadUnlockedEmotes(accounts, dialog, catalog, broadcaster, done, generation, next, page + 1);
            }
        },
        "GET", params);
}

void Pane::openEmotes() {
    auto *s = targetSource();
    if (!s) {
        return;
    }
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(hub->cfg->T("Emotes in {channel}", {{"channel", s->name()}}));
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    auto *search = new QLineEdit(dialog);
    search->setObjectName("emote-search");
    search->setPlaceholderText(hub->cfg->T("Filter emotes in this tab…"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);
    auto *tabs = new QTabBar(dialog);
    tabs->setObjectName("emote-tabs");
    tabs->setDrawBase(false);
    for (const auto &tab : QStringList{"Subs", "Channel", "Global", "Emojis"}) {
        tabs->addTab(hub->cfg->T(tab));
    }
    tabs->setTabEnabled(0, s->platform == "twitch");
    layout->addWidget(tabs);
    auto *grid = new QListWidget(dialog);
    grid->setObjectName("emote-grid");
    grid->setViewMode(QListView::IconMode);
    grid->setIconSize(QSize(38, 38));
    grid->setGridSize(QSize(96, 72));
    grid->setResizeMode(QListView::Adjust);
    grid->setMovement(QListView::Static);
    grid->setUniformItemSizes(true);
    grid->setWordWrap(false);
    grid->setTextElideMode(Qt::ElideRight);
    grid->setFrameShape(QFrame::NoFrame);
    layout->addWidget(grid, 1);
    auto *notice = new QLabel(dialog);
    notice->setObjectName("emote-status");
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    notice->setStyleSheet("color:#8b8b94;");
    layout->addWidget(notice);
    auto *authorize = new QPushButton(dialog);
    authorize->setObjectName("subscriber-emotes-action");
    layout->addWidget(authorize);
    auto *info = new QLabel(dialog);
    auto *navigation = new QHBoxLayout;
    auto *previous = new QPushButton(hub->cfg->T("Previous"), dialog);
    auto *next = new QPushButton(hub->cfg->T("Next"), dialog);
    navigation->addWidget(previous);
    navigation->addWidget(info, 1);
    navigation->addWidget(next);
    layout->addLayout(navigation);
    auto page = QSharedPointer<int>::create(0);
    auto unlocked = QSharedPointer<UnlockedEmotes>::create();
    QPointer<Source> source(s);
    QPointer<QDialog> safe(dialog);
    auto populate = [=, this] {
        if (!source || !safe) {
            return;
        }
        auto &accounts = *hub->accounts;
        const bool allowed = accounts.twitchAllows({"user:read:emotes"});
        const bool busy = accounts.twitchBusy || accounts.twitchRefreshing || unlocked->loading;
        const bool subscribers = source->platform == "twitch" && tabs->currentIndex() == 0;
        authorize->setVisible(subscribers);
        authorize->setEnabled(!busy);
        authorize->setText(hub->cfg->T(busy                           ? "Loading subscriber emotes…"
                                       : allowed                      ? "Refresh subscriber emotes"
                                       : accounts.connected("twitch") ? "Enable Twitch subscriber emotes…"
                                                                      : "Sign in to Twitch…"));
        if (subscribers) {
            notice->setText(
                !unlocked->error.isEmpty() ? hub->cfg->T(unlocked->error)
                : busy                     ? hub->cfg->T("Loading subscriber emotes…")
                : allowed ? hub->cfg->T("Subscriber emotes enabled for @{name}",
                                        {{"name", str(accounts.twitch.value("login"))}})
                          : hub->cfg->T(
                                "Connect Twitch and enable subscriber emotes to see your unlocked emotes."));
        }
        notice->setVisible(subscribers);
        QHash<QString, Span> catalog = tabs->currentIndex() == 0   ? unlocked->items
                                       : tabs->currentIndex() == 1 ? source->channelEmotes
                                                                   : hub->globals;
        if (tabs->currentIndex() == 2 && source->platform == "kick")
            for (auto i = source->kickGlobalEmotes.cbegin(); i != source->kickGlobalEmotes.cend(); ++i) catalog[i.key()] = i.value();
        grid->clear();
        QStringList codes = catalog.keys();
        codes.sort(Qt::CaseInsensitive);
        const auto needle = search->text().trimmed();
        int total = 0;
        for (const auto &code : codes) {
            const auto emote = catalog.value(code);
            if (!emote.code.contains(needle, Qt::CaseInsensitive) || tabs->currentIndex() == 3) {
                continue;
            }
            const int position = total++;
            if (position < *page * 200 || position >= (*page + 1) * 200) {
                continue;
            }
            auto *item = new QListWidgetItem(emote.code, grid);
            item->setSizeHint(grid->gridSize());
            item->setToolTip(emote.code);
            item->setData(Qt::UserRole, emote.code);
            item->setData(Qt::UserRole + 1, emote.urls.value(0));
            const auto image = hub->images->get(emote.urls);
            if (!image.isNull()) {
                item->setIcon(QPixmap::fromImage(image));
            }
        }
        if (tabs->currentIndex() == 3) {
            for (const auto &emoji :
                 QStringList{"😀", "😂", "❤️", "👍", "👀", "🔥", "🎉", "😭", "💀", "🙏", "😎", "🤔"}) {
                if (!emoji.contains(needle)) {
                    continue;
                }
                auto *item = new QListWidgetItem(emoji, grid);
                item->setSizeHint(grid->gridSize());
                item->setData(Qt::UserRole, emoji);
            }
            total = grid->count();
        }
        previous->setEnabled(*page > 0);
        next->setEnabled((*page + 1) * 200 < total);
        previous->setVisible(total > 200 || *page > 0);
        next->setVisible(total > 200 || *page > 0);
        info->setText(hub->cfg->T("{count} emotes · page {page}",
                                  {{"count", QString::number(total)}, {"page", QString::number(*page + 1)}}));
    };
    auto load = [=, this] {
        if (!safe || !source || unlocked->loading || !hub->accounts->twitchAllows({"user:read:emotes"})) {
            return;
        }
        unlocked->loading = true;
        unlocked->error.clear();
        unlocked->user = str(hub->accounts->twitch.value("user_id"));
        unlocked->items.clear();
        ++unlocked->generation;
        populate();
        loadUnlockedEmotes(hub->accounts, safe, unlocked, source->room(), populate, unlocked->generation);
    };
    connect(search, &QLineEdit::textChanged, dialog, [=] {
        *page = 0;
        populate();
    });
    connect(tabs, &QTabBar::currentChanged, dialog, [=] {
        *page = 0;
        populate();
    });
    connect(previous, &QPushButton::clicked, dialog, [=] {
        *page = qMax(0, *page - 1);
        populate();
    });
    connect(next, &QPushButton::clicked, dialog, [=] {
        ++*page;
        populate();
    });
    auto insert = [this, safe](QListWidgetItem *item) {
        if (!safe || !safe->isVisible() || input->isReadOnly()) {
            return;
        }
        input->insert(item->data(Qt::UserRole).toString() + ' ');
    };
    connect(grid, &QListWidget::itemClicked, dialog, insert);
    connect(grid, &QListWidget::itemActivated, dialog, insert);
    connect(hub->images, &Images::ready, dialog, [this, grid](QString url) {
        for (int i = 0; i < grid->count(); ++i) {
            auto *item = grid->item(i);
            if (item->data(Qt::UserRole + 1).toString() == url) {
                const auto image = hub->images->get({url});
                if (!image.isNull()) {
                    item->setIcon(QPixmap::fromImage(image));
                }
            }
        }
    });
    connect(s, &Source::changed, dialog, populate);
    connect(authorize, &QPushButton::clicked, dialog, [=, this] {
        if (hub->accounts->twitchAllows({"user:read:emotes"})) {
            load();
        } else {
            hub->accounts->requireTwitch({"user:read:emotes"}, [=](QString, QString error) {
                if (safe) {
                    unlocked->error = error;
                    if (error.isEmpty()) {
                        load();
                    }
                    populate();
                }
            });
        }
    });
    connect(hub->accounts, &Accounts::changed, dialog, [=, this] {
        const auto user = str(hub->accounts->twitch.value("user_id"));
        if (user != unlocked->user && !unlocked->user.isEmpty()) {
            ++unlocked->generation;
            unlocked->items.clear();
            unlocked->loading = false;
            unlocked->user.clear();
        }
        if (source && source->platform == "twitch" && unlocked->user.isEmpty() &&
            hub->accounts->twitchAllows({"user:read:emotes"})) {
            load();
        }
        populate();
    });
    if (s->platform != "twitch" || !hub->accounts->twitchAllows({"user:read:emotes"})) {
        tabs->setCurrentIndex(1);
    }
    populate();
    dialog->resize(620, 460);
    dialog->show();
    darkTitlebar(dialog);
    if (s->platform == "twitch") {
        load();
    }
}

Workspace::Workspace(Hub *h, QWidget *p)
    : QWidget(p), hub(h), uid(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
}

QList<Pane *> Workspace::panes() const {
    return findChildren<Pane *>();
}

void Workspace::toggleZoom(Pane *pane) {
    for (auto w : zoomHidden) {
        if (w) {
            w->show();
        }
    }
    zoomHidden.clear();
    if (zoomed == pane) {
        zoomed = nullptr;
        return;
    }
    zoomed = pane;
    QWidget *branch = pane;
    while (branch && branch->parentWidget() != this) {
        auto *split = qobject_cast<QSplitter *>(branch->parentWidget());
        if (!split) {
            break;
        }
        for (int i = 0; i < split->count(); ++i) {
            if (split->widget(i) != branch) {
                zoomHidden << split->widget(i);
                split->widget(i)->hide();
            }
        }
        branch = split;
    }
}

Pane *Workspace::add(QList<Source *> sources, Pane *anchor, Qt::Orientation orientation) {
    if (zoomed) {
        toggleZoom(zoomed);
    }
    auto *pane = new Pane(hub, sources, this);
    if (!layout->count()) {
        layout->addWidget(pane);
    } else {
        QWidget *old = anchor ? static_cast<QWidget *>(anchor) : layout->itemAt(0)->widget();
        auto *parentSplitter = qobject_cast<QSplitter *>(old->parentWidget());
        int index = parentSplitter ? parentSplitter->indexOf(old) : 0;
        auto *splitter = new QSplitter(orientation);
        splitter->setChildrenCollapsible(false);
        if (parentSplitter) {
            parentSplitter->replaceWidget(index, splitter);
        } else {
            layout->removeWidget(old);
            layout->addWidget(splitter);
        }
        splitter->addWidget(old);
        splitter->addWidget(pane);
        splitter->setSizes({1, 1});
        connect(splitter, &QSplitter::splitterMoved, this, [this] { emit changed(); });
    }
    focusedPane = pane;
    connect(pane, &Pane::focused, this, [this](Pane *p) { focusedPane = p; });
    emit paneAdded(pane);
    emit changed();
    return pane;
}

void Workspace::closePane(Pane *pane) {
    if (zoomed) {
        toggleZoom(zoomed);
    }
    auto *split = qobject_cast<QSplitter *>(pane->parentWidget());
    if (!split) {
        layout->removeWidget(pane);
        pane->deleteLater();
        focusedPane = nullptr;
        emit changed();
        return;
    }
    pane->setParent(nullptr);
    pane->deleteLater();
    if (split->count() == 1) {
        QWidget *keep = split->widget(0);
        keep->setParent(nullptr);
        auto *outer = qobject_cast<QSplitter *>(split->parentWidget());
        if (outer) {
            int index = outer->indexOf(split);
            outer->replaceWidget(index, keep);
        } else {
            layout->removeWidget(split);
            layout->addWidget(keep);
        }
        split->deleteLater();
    }
    auto ps = panes();
    focusedPane = ps.value(0);
    emit changed();
}

QJsonObject Workspace::serializeNode(QWidget *node) const {
    if (auto *p = qobject_cast<Pane *>(node)) {
        return p->serialize();
    }
    if (auto *s = qobject_cast<QSplitter *>(node)) {
        QJsonArray children, sizes;
        for (int i = 0; i < s->count(); ++i) {
            children.append(serializeNode(s->widget(i)));
        }
        for (auto size : s->sizes()) {
            sizes.append(size);
        }
        return {{"orientation", s->orientation() == Qt::Horizontal ? "horizontal" : "vertical"},
                {"children", children},
                {"sizes", sizes}};
    }
    return {};
}

QJsonObject Workspace::serialize() const {
    return {{"id", uid},
            {"name", title},
            {"tree", layout->count() ? serializeNode(layout->itemAt(0)->widget()) : QJsonObject()},
            {"focused", focusedPane ? focusedPane->uid : QString()}};
}

QWidget *Workspace::restoreNode(const QJsonObject &node, int depth) {
    if (depth > 24 || node.isEmpty()) {
        return nullptr;
    }
    if (node.contains("sources")) {
        QList<Source *> list;
        for (const auto &k : node["sources"].toArray()) {
            QString key = k.toString();
            if (!hub->sources.contains(key)) {
                if (key.startsWith("twitch:") && !twitchLogin('@' + key.mid(7)).isEmpty()) {
                    hub->source("twitch", key.mid(7));
                } else if (key.startsWith("kick:") && kickLogin(key) == key.mid(5)) {
                    hub->source("kick", key.mid(5));
                } else if (videoId(key) == key) {
                    hub->source("youtube", key);
                }
            }
            if (hub->sources.contains(k.toString())) {
                list << hub->sources[k.toString()];
            }
        }
        if (list.isEmpty()) {
            return nullptr;
        }
        auto *p = new Pane(hub, list, this);
        p->headerLines = qBound(0, node["header_lines"].toInt(), 100);
        p->applyHeaderLineLimit();
        p->uid = str(node["pane_id"]);
        if (p->uid.isEmpty()) {
            p->uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        p->setSources(list);
        connect(p, &Pane::focused, this, [this](Pane *p) { focusedPane = p; });
        emit paneAdded(p);
        return p;
    }
    auto *s = new QSplitter(
        node["orientation"].toString(node["orient"].toString()) == "vertical" ? Qt::Vertical : Qt::Horizontal,
        this);
    s->setChildrenCollapsible(false);
    int count = 0;
    for (const auto &v : node["children"].toArray()) {
        if (++count > 24) {
            break;
        }
        if (auto *child = restoreNode(v.toObject(), depth + 1)) {
            s->addWidget(child);
        }
    }
    QList<int> sizes;
    for (const auto &v : node["sizes"].toArray()) {
        sizes << qMax(1, v.toInt());
    }
    if (sizes.size() == s->count()) {
        s->setSizes(sizes);
    } else if (s->count() == 2) {
        double ratio = qBound(.05, node["ratio"].toDouble(.5), .95);
        s->setSizes({int(ratio * 1000), int((1 - ratio) * 1000)});
    }
    connect(s, &QSplitter::splitterMoved, this, [this] { emit changed(); });
    return s;
}

void Workspace::restore(const QJsonObject &saved) {
    uid = str(saved["id"]);
    title = str(saved["name"]);
    auto tree = saved["tree"].toObject();
    if (tree.isEmpty()) {
        tree = saved["layout"].toObject();
    }
    if (auto *root = restoreNode(tree)) {
        layout->addWidget(root);
    }
    auto ps = panes();
    focusedPane = ps.value(0);
    for (auto *p : ps) {
        if (p->uid == str(saved["focused"])) {
            focusedPane = p;
        }
    }
}
} // namespace yc
