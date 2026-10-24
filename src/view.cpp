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
        latest->setVisible(!follow && verticalScrollBar()->maximum() > 0);
        viewport()->update();
    });
    resizeTimer.setSingleShot(true);
    resizeTimer.setInterval(24);
    connect(&resizeTimer, &QTimer::timeout, this, &ChatView::reflow);
    connect(hub->cfg, &Config::appearanceChanged, this, &ChatView::invalidate);
    connect(hub->cfg, &Config::languageChanged, this, &ChatView::invalidate);
    connect(hub->images, &Images::ready, this, [this](const QString &url) {
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
    for (auto &m : messages) {
        if (!m.seq) {
            m.seq = synthetic--;
        }
    }
    documents.clear();
    documentUse.clear();
    details.clear();
    heights.clear();
    for (const auto &m : messages) {
        heights << estimate(m);
    }
    trim();
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
    messages << m;
    heights << estimate(m);
    trim();
    rebuildOffsets();
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
    QString text = m.name + ": " + m.text;
    int lines = qMax(1, (fm.horizontalAdvance(text) + width - 1) / width);
    lines += text.count('\n');
    int height = lines * fm.lineSpacing() + 8;
    if (!m.reply.isEmpty() && !threadMode) {
        height += fm.lineSpacing();
    }
    if (!m.emotes.isEmpty()) {
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

static QImage roleBadge(const QString &role, int size) {
    QImage image(size * 2, size * 2, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(image.width() / 18., image.height() / 18.);
    p.setPen(Qt::NoPen);
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
    if (a["show_timestamps"].toBool(true) && m.stamp.isValid()) {
        cursor.insertText(hub->cfg->time(m.stamp) + ' ', format(normal, dim));
    }
    if (m.system) {
        cursor.insertText(hub->cfg->T(m.text), format(normal, dim));
        documents[m.seq] = doc;
        details[m.seq] = detail;
        return doc;
    }
    if (mixed && source) {
        cursor.insertText('[' + source->name() + "] ",
                          format(normal, QColor(source->platform == "twitch" ? "#b99aff" : "#ff7777")));
    }
    auto insertImage = [&](QStringList urls, QString code, int size, bool gif) {
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
        detail.imageSizes[urls[0]] = fitted;
        doc->addResource(QTextDocument::ImageResource, QUrl(urls[0]),
                         image.scaled(fitted, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        cursor.insertImage(f);
    };
    for (auto badge : m.badges) {
        if (badge.urls.isEmpty() && source) {
            auto full = source->badges.value(badge.id + '/' + badge.version);
            if (!full.urls.isEmpty()) {
                badge = full;
            }
        }
        if (badge.urls.isEmpty() && (badge.role == "owner" || badge.role == "moderator" ||
                                     badge.role == "subscriber" || badge.role == "vip")) {
            const int size = a["badge_size"].toInt(18);
            QString url = "badge:" + badge.role;
            doc->addResource(QTextDocument::ImageResource, QUrl(url), roleBadge(badge.role, size));
            QTextImageFormat f;
            f.setName(url);
            f.setWidth(size);
            f.setHeight(size);
            f.setVerticalAlignment(QTextCharFormat::AlignMiddle);
            detail.imageCodes[cursor.position()] = '[' + badge.role + ']';
            cursor.insertImage(f);
            cursor.insertText(" ", normalFormat);
        } else {
            insertImage(badge.urls, badge.label.isEmpty() ? '[' + badge.id + ']' : badge.label,
                        a["badge_size"].toInt(18), false);
            cursor.insertText(" ", normalFormat);
        }
    }
    detail.nameStart = cursor.position();
    cursor.insertText(m.name, format(username, userColor(m)));
    detail.nameEnd = cursor.position();
    cursor.insertText(": ", normalFormat);
    QString body = m.deleted ? hub->cfg->T("[message deleted]") : m.text;
    QVector<Span> spans = m.deleted ? QVector<Span>() : m.emotes;
    if (source && !m.deleted) {
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
    std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) {
        return a.start == b.start ? a.end > b.end : a.start < b.start;
    });
    int offset = 0;
    auto insertText = [&](const QString &text) {
        auto links = QRegularExpression("https?://[^\\s<>]+").globalMatch(text);
        int from = 0;
        while (links.hasNext()) {
            auto match = links.next();
            cursor.insertText(text.mid(from, match.capturedStart() - from), normalFormat);
            auto lf = normalFormat;
            lf.setAnchor(true);
            lf.setAnchorHref(match.captured());
            lf.setForeground(QColor("#4a9cdb"));
            cursor.insertText(match.captured(), lf);
            from = match.capturedEnd();
        }
        cursor.insertText(text.mid(from), normalFormat);
    };
    for (const auto &e : spans) {
        if (e.start < offset || e.end > body.size() || e.end <= e.start) {
            continue;
        }
        insertText(body.mid(offset, e.start - offset));
        insertImage(e.urls, e.code.isEmpty() ? body.mid(e.start, e.end - e.start) : e.code,
                    a["emote_size"].toInt(36), e.gif);
        offset = e.end;
    }
    insertText(body.mid(offset));
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
    for (int i = qMax(0, row); i < messages.size() && offsets.value(i) - scroll < viewport()->height(); ++i) {
        auto doc = document(i);
        if (!doc) {
            continue;
        }
        const auto detail = details.value(messages[i].seq);
        for (auto it = detail.imageSizes.begin(); it != detail.imageSizes.end(); ++it) {
            auto image = hub->images->get({it.key()});
            if (!image.isNull()) {
                doc->addResource(QTextDocument::ImageResource, QUrl(it.key()),
                                 image.scaled(it.value(), Qt::KeepAspectRatio, Qt::FastTransformation));
            }
        }
        int height = qCeil(doc->size().height()) + (a["compact_spacing"].toBool() ? 4 : 8);
        if (height != heights[i]) {
            heights[i] = height;
            changed = true;
        }
        int y = offsets.value(i) - scroll;
        QRect rect(0, y, viewport()->width(), height);
        QColor background = highlight(messages[i], *hub->cfg, hub->accounts->names());
        if (!background.isValid()) {
            background = a["alternate_rows"].toBool(true) && i % 2 ? QColor("#0e0e0e") : QColor(Qt::black);
        }
        painter.fillRect(rect, background);
        painter.save();
        painter.translate(6, y + 3);
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
    }
    if (changed) {
        QTimer::singleShot(0, this, [this] {
            rebuildOffsets();
            if (follow) {
                verticalScrollBar()->setValue(verticalScrollBar()->maximum());
            }
            viewport()->update();
        });
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

void ChatView::mousePressEvent(QMouseEvent *e) {
    emit focused();
    setFocus(Qt::MouseFocusReason);
    if (e->button() != Qt::LeftButton) {
        return;
    }
    pressPoint = e->position().toPoint();
    selectionStart = selectionEnd = positionAt(pressPoint);
    dragging = false;
    viewport()->update();
}

void ChatView::mouseMoveEvent(QMouseEvent *e) {
    if (e->buttons().testFlag(Qt::LeftButton) && selectionStart.first >= 0) {
        dragging |= (e->position().toPoint() - pressPoint).manhattanLength() > 4;
        selectionEnd = positionAt(e->position().toPoint());
        follow = false;
        viewport()->update();
    } else {
        auto pos = positionAt(e->position().toPoint());
        bool clickable = false;
        if (pos.first >= 0) {
            auto d = details.value(messages[pos.first].seq);
            clickable = (pos.second >= d.nameStart && pos.second < d.nameEnd) ||
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
    int row = rowAt(e->pos().y() + verticalScrollBar()->value());
    if (row >= 0) {
        emit messageMenuRequested(messages[row], e->globalPos());
    }
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
    if (e->matches(QKeySequence::Copy)) {
        copySelection();
        return;
    }
    if (e->matches(QKeySequence::SelectAll) && !messages.isEmpty()) {
        selectionStart = {0, 0};
        selectionEnd = {int(messages.size()) - 1, document(messages.size() - 1)->characterCount() - 1};
        follow = false;
        viewport()->update();
        return;
    }
    if (e->key() == Qt::Key_End) {
        jumpLatest();
        return;
    }
    if (e->key() == Qt::Key_Home) {
        follow = false;
        verticalScrollBar()->setValue(0);
        return;
    }
    QAbstractScrollArea::keyPressEvent(e);
}

void ChatView::wheelEvent(QWheelEvent *e) {
    follow = false;
    QAbstractScrollArea::wheelEvent(e);
    if (verticalScrollBar()->value() >= verticalScrollBar()->maximum() - 2) {
        follow = true;
        latest->hide();
    }
}

void ChatView::jumpLatest() {
    follow = true;
    selectionStart = selectionEnd = {-1, 0};
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    latest->hide();
    viewport()->update();
}

void ChatView::jumpRow(int row) {
    follow = false;
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

void Composer::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        emit escapePressed();
        return;
    }
    if (e->key() == Qt::Key_Up && text().isEmpty()) {
        setText(lastSent);
        return;
    }
    if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) {
        emit completeRequested();
        int direction = e->key() == Qt::Key_Backtab || e->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1;
        QString current = text();
        if (completionValue != current || completions.isEmpty()) {
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
            completionIndex = direction > 0 ? 0 : completions.size() - 1;
        } else {
            completionIndex = (completionIndex + direction + completions.size()) % completions.size();
        }
        if (completions.isEmpty()) {
            return;
        }
        QString word = completions[completionIndex];
        completionValue = completionHead + word +
                          (completionTail.startsWith(' ') ? QString() : QString(" ")) + completionTail;
        setText(completionValue);
        setCursorPosition(completionHead.size() + word.size() + 1);
        return;
    }
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
    hl->addWidget(headerIcon);
    headerText = new QLabel(header);
    headerText->setWordWrap(true);
    headerText->setTextFormat(Qt::PlainText);
    headerText->setFont(QFont("Segoe UI", 9, QFont::Bold));
    hl->addWidget(headerText, 1);
    auto *actions = new QToolButton(header);
    actions->setText("▾");
    hl->addWidget(actions);
    layout->addWidget(header);
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
    input = new Composer(composer);
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
    connect(input, &QLineEdit::textChanged, this, [this](QString text) {
        int limit = targetSource() && targetSource()->platform == "youtube" ? 200 : 500;
        countLabel->setText(text.isEmpty() ? QString()
                                           : QString("%1/%2").arg(characterCount(text)).arg(limit));
    });
    connect(input, &Composer::completeRequested, this, [this] {
        QStringList c;
        auto s = targetSource();
        if (s) {
            c = s->emotes.keys();
            for (const auto &m : s->history) {
                QString name = m.login.isEmpty() ? m.name : m.login;
                if (!name.isEmpty()) {
                    c << '@' + name;
                }
            }
        }
        input->candidates = c;
    });
    connect(target, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
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
    connect(hub->cfg, &Config::languageChanged, this, &Pane::refresh);
    setSources(list);
    if (detached) {
        header->hide();
        target->hide();
        view->threadMode = true;
    }
}

Source *Pane::targetSource() const {
    int i = target->currentIndex();
    return i >= 0 && i < sources.size() ? sources[i].data() : nullptr;
}

Pane::~Pane() {
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
        target->addItem((s->platform == "twitch" ? "TW · " : "YT · ") + s->name(), s->key);
        connect(s, &Source::message, this, [this, s](Message m) { incoming(s, m); });
        connect(s, &Source::changed, this, [this] {
            refresh();
            reload();
        });
        connect(s, &Source::redacted, this, [this](QJsonObject target) {
            if (detached || reply.id.isEmpty()) {
                return;
            }
            if (target["all"].toBool() || str(target["message_id"]) == reply.id ||
                (!str(target["user_id"]).isEmpty() && str(target["user_id"]) == reply.uid)) {
                cancelReply();
            }
        });
    }
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
}

void Pane::refresh() {
    QStringList titles;
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
            QString text = count + " - ";
            if (source->platform == "twitch" && !str(source->header["category"]).isEmpty()) {
                text += str(source->header["category"]) + " - ";
            }
            titles << text + title;
        }
    }
    headerText->setText(titles.join("   |   "));
    headerIcon->setPixmap(
        platformIcon(sources.size() == 1 && sources[0] ? sources[0]->platform : "mixed").pixmap(18, 18));
    int selected = target->currentIndex();
    for (int i = 0; i < sources.size(); ++i) {
        if (sources[i]) {
            target->setItemText(i,
                                (sources[i]->platform == "twitch" ? "TW · " : "YT · ") + sources[i]->name());
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
                if (accepts(m, filter) && (!extraFilter || extraFilter(m))) {
                    all << m;
                }
            }
        }
    }
    std::sort(all.begin(), all.end(), [](const Message &a, const Message &b) { return a.seq < b.seq; });
    view->setMessages(all);
}

void Pane::incoming(Source *s, Message m) {
    Q_UNUSED(s)
    if (detached && !threadId.isEmpty()) {
        return;
    }
    if (accepts(m, filter) && (!extraFilter || extraFilter(m))) {
        view->append(m);
    }
}

void Pane::send() {
    auto *s = targetSource();
    if (!s || sending || input->text().trimmed().isEmpty()) {
        return;
    }
    QString text = input->text();
    sending = true;
    sendButton->setEnabled(false);
    QPointer<Pane> self(this);
    hub->send(s, text, reply, [self, text](QJsonObject, QString error) {
        if (!self) {
            return;
        }
        self->sending = false;
        self->sendButton->setEnabled(true);
        if (!error.isEmpty()) {
            QMessageBox::warning(self, self->hub->cfg->T("Send message"), self->hub->cfg->T(error));
            return;
        }
        self->input->lastSent = text;
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
    if (!s || s->platform != "twitch" || m.id.isEmpty() || m.deleted) {
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
    if ((object == header || object == headerText) && event->type() == QEvent::MouseButtonDblClick) {
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

void Pane::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (!detached) {
        QTimer::singleShot(0, this, [this] {
            if (headerText->width() > 50) {
                int h = qBound(24, headerText->heightForWidth(headerText->width()) + 6, 130);
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
    return {{"sources", keys}, {"pane_id", uid}};
}

static void loadUnlockedEmotes(Accounts *accounts, QPointer<QDialog> dialog,
                               QSharedPointer<QHash<QString, Span>> catalog, std::function<void()> done,
                               QString cursor = {}, int page = 0) {
    if (!dialog || page >= 50) {
        if (dialog) {
            done();
        }
        return;
    }
    QMap<QString, QString> params{{"user_id", str(accounts->twitch["user_id"])}, {"first", "100"}};
    if (!cursor.isEmpty()) {
        params["after"] = cursor;
    }
    accounts->helix(
        "chat/emotes/user",
        [accounts, dialog, catalog, done, page, cursor](QJsonObject d, QString error) {
            if (!dialog) {
                return;
            }
            for (auto v : d["data"].toArray()) {
                auto o = v.toObject();
                Span e;
                e.code = str(o["name"]);
                e.urls = {"https://static-cdn.jtvnw.net/emoticons/v2/" + str(o["id"]) + "/default/dark/3.0"};
                if (!e.code.isEmpty()) {
                    (*catalog)["__sub__" + e.code] = e;
                }
            }
            done();
            auto next = str(at(d, {"pagination", "cursor"}));
            if (error.isEmpty() && !next.isEmpty() && next != cursor) {
                loadUnlockedEmotes(accounts, dialog, catalog, done, next, page + 1);
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
    auto *search = new QLineEdit(dialog);
    search->setPlaceholderText(hub->cfg->T("Search all emotes…"));
    layout->addWidget(search);
    auto *tabs = new QTabBar(dialog);
    for (const auto &t : QStringList{"Subs", "Channel", "Global", "Emojis"}) {
        tabs->addTab(hub->cfg->T(t));
    }
    layout->addWidget(tabs);
    auto *grid = new QListWidget(dialog);
    grid->setViewMode(QListView::IconMode);
    grid->setIconSize(QSize(38, 38));
    grid->setGridSize(QSize(94, 70));
    grid->setResizeMode(QListView::Adjust);
    grid->setMovement(QListView::Static);
    layout->addWidget(grid, 1);
    auto *authorize = new QPushButton(hub->cfg->T("Enable Twitch subscriber emotes…"), dialog);
    authorize->setVisible(s->platform == "twitch");
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
    auto catalog = QSharedPointer<QHash<QString, Span>>::create(s->emotes);
    QPointer<Source> source(s);
    QPointer<QDialog> safe(dialog);
    auto populate = [this, grid, search, tabs, catalog, source, info, page, previous, next] {
        if (!source) {
            return;
        }
        grid->clear();
        QString needle = search->text();
        QStringList codes = catalog->keys();
        codes.sort(Qt::CaseInsensitive);
        int total = 0;
        for (const auto &code : codes) {
            if (!needle.isEmpty() && !code.contains(needle, Qt::CaseInsensitive)) {
                continue;
            }
            auto e = catalog->value(code);
            bool global = hub->globals.contains(code), sub = code.startsWith("__sub__");
            if (tabs->currentIndex() == 0 && !sub && needle.isEmpty()) {
                continue;
            }
            if (tabs->currentIndex() == 1 && global && needle.isEmpty()) {
                continue;
            }
            if (tabs->currentIndex() == 2 && !global && needle.isEmpty()) {
                continue;
            }
            if (tabs->currentIndex() == 3) {
                continue;
            }
            int position = total++;
            if (position < *page * 200 || position >= (*page + 1) * 200) {
                continue;
            }
            QString label = code;
            label.remove("__sub__");
            auto *item = new QListWidgetItem(label, grid);
            item->setData(Qt::UserRole, label);
            item->setData(Qt::UserRole + 1, e.urls.value(0));
            auto image = hub->images->get(e.urls);
            if (!image.isNull()) {
                item->setIcon(QPixmap::fromImage(image));
            }
        }
        if (tabs->currentIndex() == 3) {
            for (const auto &emoji :
                 QStringList{"😀", "😂", "❤️", "👍", "👀", "🔥", "🎉", "😭", "💀", "🙏", "😎", "🤔"}) {
                auto *item = new QListWidgetItem(emoji, grid);
                item->setData(Qt::UserRole, emoji);
            }
        }
        if (tabs->currentIndex() == 3) {
            total = grid->count();
        }
        previous->setEnabled(*page > 0);
        next->setEnabled((*page + 1) * 200 < total);
        info->setText(hub->cfg->T("{count} emotes · page {page}",
                                  {{"count", QString::number(total)}, {"page", QString::number(*page + 1)}}));
    };
    connect(search, &QLineEdit::textChanged, dialog, [populate, page] {
        *page = 0;
        populate();
    });
    connect(tabs, &QTabBar::currentChanged, dialog, [populate, page] {
        *page = 0;
        populate();
    });
    connect(previous, &QPushButton::clicked, dialog, [page, populate] {
        *page = qMax(0, *page - 1);
        populate();
    });
    connect(next, &QPushButton::clicked, dialog, [page, populate] {
        ++*page;
        populate();
    });
    connect(grid, &QListWidget::itemClicked, dialog, [this, safe](QListWidgetItem *item) {
        input->insert(item->data(Qt::UserRole).toString() + ' ');
        input->setFocus();
        if (safe) {
            safe->close();
        }
    });
    connect(hub->images, &Images::ready, dialog, [this, grid](QString url) {
        for (int i = 0; i < grid->count(); ++i) {
            auto *item = grid->item(i);
            if (item->data(Qt::UserRole + 1).toString() == url) {
                auto image = hub->images->get({url});
                if (!image.isNull()) {
                    item->setIcon(QPixmap::fromImage(image));
                }
            }
        }
    });
    connect(authorize, &QPushButton::clicked, dialog, [this, catalog, populate, safe] {
        hub->accounts->requireTwitch(
            {"user:read:emotes"}, [this, catalog, populate, safe](QString, QString e) {
                if (!safe) {
                    return;
                }
                if (!e.isEmpty()) {
                    QMessageBox::warning(safe, hub->cfg->T("Twitch sign-in"), hub->cfg->T(e));
                    return;
                }
                loadUnlockedEmotes(hub->accounts, safe, catalog, populate);
            });
    });
    if (!hub->accounts->twitchScopes().contains("user:read:emotes")) {
        tabs->setCurrentIndex(1);
    } else {
        authorize->click();
    }
    populate();
    dialog->resize(610, 450);
    dialog->show();
    darkTitlebar(dialog);
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
