#include "line_limited_edit.hpp"

#include <algorithm>

#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSettings>
#include <QTextBlock>
#include <QTextCursor>

namespace {

constexpr QColor kRulerOkColor(255, 245, 170, 180);
constexpr QColor kRulerExceededColor(220, 60, 60, 220);
constexpr const char* kMaxLineLengthKey = "ui/maxLineLength";
constexpr const char* kJustifyKey = "ui/justifyText";

int char_column_width(const QFontMetrics& metrics) {
  return metrics.horizontalAdvance(QLatin1Char('M'));
}

bool is_blank(const QString& line) {
  return line.trimmed().isEmpty();
}

// Spezza le parole in righe entro limit; la prima riga parte con indent.
QStringList wrap_words(const QStringList& words, int limit, const QString& indent) {
  QStringList lines;
  QString current = indent;
  bool current_has_word = false;
  for(const QString& w : words) {
    if(!current_has_word) {
      current += w;
      current_has_word = true;
      continue;
    }
    if(current.size() + 1 + w.size() <= limit) {
      current += QLatin1Char(' ');
      current += w;
    } else {
      lines << current;
      current = w;
    }
  }
  if(current_has_word) {
    lines << current;
  }
  return lines;
}

// Riempie la riga fino a limit distribuendo spazi tra le parole (extra a sinistra).
QString justify_line(const QString& line, int limit) {
  int lead = 0;
  while(lead < line.size() && line.at(lead) == QLatin1Char(' ')) {
    ++lead;
  }
  const QStringList words =
      line.mid(lead).split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if(words.size() < 2) {
    return line;
  }
  int letters = lead;
  for(const auto& w : words) {
    letters += w.size();
  }
  const int gaps = static_cast<int>(words.size()) - 1;
  const int spaces = limit - letters;
  if(spaces < gaps) {
    return line;
  }
  const int base = spaces / gaps;
  int extra = spaces % gaps;
  QString out(lead, QLatin1Char(' '));
  for(int i = 0; i < words.size(); ++i) {
    out += words[i];
    if(i < gaps) {
      out += QString(base + (extra > 0 ? 1 : 0), QLatin1Char(' '));
      if(extra > 0) {
        --extra;
      }
    }
  }
  return out;
}

QString format_paragraph(const QStringList& lines, int limit, bool justify) {
  int lead = 0;
  const QString& first = lines.front();
  while(lead < first.size() && first.at(lead) == QLatin1Char(' ')) {
    ++lead;
  }
  const QString indent = first.left(lead);
  QString joined = lines.join(QLatin1Char(' '));
  joined.replace(QChar(0x00a0), QLatin1Char(' '));
  const QStringList words =
      joined.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  QStringList out = wrap_words(words, limit, indent);
  if(justify) {
    for(int i = 0; i + 1 < out.size(); ++i) {
      out[i] = justify_line(out[i], limit);
    }
  }
  return out.join(QLatin1Char('\n'));
}

}  // namespace

// ---------------------------------------------------------------------------

MudTextFormat& MudTextFormat::instance() {
  static MudTextFormat* inst = new MudTextFormat();
  return *inst;
}

MudTextFormat::MudTextFormat() {
  QSettings settings(QStringLiteral("NebbieArcane"), QStringLiteral("Izanagi"));
  max_length_ = settings.value(QString::fromUtf8(kMaxLineLengthKey), 0).toInt();
  if(max_length_ < 0 || max_length_ > 512) {
    max_length_ = 0;
  }
  justify_ = settings.value(QString::fromUtf8(kJustifyKey), false).toBool();
}

void MudTextFormat::set(int max_length, bool justify) {
  max_length = std::max(0, std::min(512, max_length));
  if(max_length == max_length_ && justify == justify_) {
    return;
  }
  max_length_ = max_length;
  justify_ = justify;
  QSettings settings(QStringLiteral("NebbieArcane"), QStringLiteral("Izanagi"));
  settings.setValue(QString::fromUtf8(kMaxLineLengthKey), max_length_);
  settings.setValue(QString::fromUtf8(kJustifyKey), justify_);
  emit changed();
}

QString reflow_mud_text(const QString& text, int max_len, bool justify) {
  if(max_len <= 0) {
    return text;
  }
  QString normalized = text;
  normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  const bool trailing_newline = normalized.endsWith(QLatin1Char('\n'));
  const QStringList lines = normalized.split(QLatin1Char('\n'));
  QStringList out;
  QStringList paragraph;
  auto flush = [&]() {
    if(!paragraph.isEmpty()) {
      out << format_paragraph(paragraph, max_len, justify);
      paragraph.clear();
    }
  };
  for(const QString& line : lines) {
    if(is_blank(line)) {
      flush();
      out << QString();
    } else {
      paragraph << line;
    }
  }
  flush();
  QString result = out.join(QLatin1Char('\n'));
  // split() su testo che finisce con \n produce una riga vuota finale: gia' inclusa.
  if(!trailing_newline) {
    while(result.endsWith(QLatin1Char('\n'))) {
      result.chop(1);
    }
  }
  return result;
}

// ---------------------------------------------------------------------------

LineLimitedPlainTextEdit::LineLimitedPlainTextEdit(QWidget* parent) : QPlainTextEdit(parent) {
  connect(this, &QPlainTextEdit::textChanged, this, [this]() {
    refreshRulerState();
    viewport()->update();
  });
  connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() { viewport()->update(); });
  auto& fmt = MudTextFormat::instance();
  max_line_length_ = fmt.maxLength();
  justify_ = fmt.justify();
  connect(&fmt, &MudTextFormat::changed, this, &LineLimitedPlainTextEdit::onFormatChanged);
}

void LineLimitedPlainTextEdit::onFormatChanged() {
  auto& fmt = MudTextFormat::instance();
  justify_ = fmt.justify();
  setMaxLineLength(fmt.maxLength());
  // Solo i campi visibili: non si toccano testi di altre schede/aree che non stai guardando.
  if(auto_wrap_ && !isReadOnly() && isVisible() && !toPlainText().isEmpty()) {
    reflowAll();
  }
}

void LineLimitedPlainTextEdit::setMaxLineLength(int max_length) {
  max_line_length_ = max_length < 0 ? 0 : max_length;
  refreshRulerState();
  viewport()->update();
}

void LineLimitedPlainTextEdit::refreshRulerState() {
  ruler_exceeded_ = anyLineExceedsLimit();
}

bool LineLimitedPlainTextEdit::anyLineExceedsLimit() const {
  if(max_line_length_ <= 0) {
    return false;
  }
  const QString text = toPlainText();
  int start = 0;
  while(start <= text.size()) {
    int end = text.indexOf(QLatin1Char('\n'), start);
    if(end < 0) {
      end = text.size();
    }
    if((end - start) > max_line_length_) {
      return true;
    }
    if(end >= text.size()) {
      break;
    }
    start = end + 1;
  }
  return false;
}

void LineLimitedPlainTextEdit::reflowAll() {
  if(max_line_length_ <= 0) {
    return;
  }
  const QString before = toPlainText();
  const QString after = reflow_mud_text(before, max_line_length_, justify_);
  if(after == before) {
    return;
  }
  reflowing_ = true;
  QTextCursor cursor(document());
  cursor.beginEditBlock();
  cursor.select(QTextCursor::Document);
  cursor.insertText(after);
  cursor.endEditBlock();
  reflowing_ = false;
}

void LineLimitedPlainTextEdit::keyPressEvent(QKeyEvent* event) {
  QPlainTextEdit::keyPressEvent(event);
  // Invio: a capo voluto, non si reimpagina (due Invio = nuovo paragrafo).
  if(event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    return;
  }
  if(!event->text().isEmpty() || event->key() == Qt::Key_Backspace ||
     event->key() == Qt::Key_Delete) {
    reflowCurrentParagraph();
  }
}

void LineLimitedPlainTextEdit::insertFromMimeData(const QMimeData* source) {
  QPlainTextEdit::insertFromMimeData(source);
  reflowCurrentParagraph();
}

void LineLimitedPlainTextEdit::reflowCurrentParagraph() {
  if(!auto_wrap_ || reflowing_ || max_line_length_ <= 0 || isReadOnly()) {
    return;
  }
  QTextCursor cur = textCursor();
  QTextBlock block = cur.block();
  if(is_blank(block.text())) {
    return;
  }
  // Limiti del paragrafo: righe non vuote contigue.
  QTextBlock first = block;
  while(first.previous().isValid() && !is_blank(first.previous().text())) {
    first = first.previous();
  }
  QTextBlock last = block;
  while(last.next().isValid() && !is_blank(last.next().text())) {
    last = last.next();
  }
  QStringList lines;
  bool overflow = false;
  for(QTextBlock b = first; b.isValid(); b = b.next()) {
    lines << b.text();
    overflow = overflow || b.text().size() > max_line_length_;
    if(b == last) {
      break;
    }
  }
  if(!overflow && !justify_) {
    return;  // senza giustificazione si interviene solo quando si supera il limite
  }
  const int para_start = first.position();
  const int para_end = last.position() + last.length() - 1;  // esclude il \n finale
  const QString old_text = lines.join(QLatin1Char('\n'));
  const int cursor_in_para = cur.position() - para_start;

  QString new_text = format_paragraph(lines, max_line_length_, justify_);
  // Se si sta scrivendo in fondo e l'ultimo carattere e' uno spazio, tienilo:
  // altrimenti la parola successiva si attaccherebbe alla precedente.
  const bool at_end = cursor_in_para >= old_text.size();
  if(at_end && !old_text.isEmpty() && old_text.back().isSpace()) {
    new_text += QLatin1Char(' ');
  }
  if(new_text == old_text) {
    return;
  }

  // Posizione del cursore: stessa quantita' di caratteri non-spazio prima di esso.
  int non_space = 0;
  for(int i = 0; i < cursor_in_para && i < old_text.size(); ++i) {
    if(!old_text.at(i).isSpace()) {
      ++non_space;
    }
  }
  const bool after_space = cursor_in_para > 0 && cursor_in_para <= old_text.size() &&
                           old_text.at(cursor_in_para - 1).isSpace();
  int new_pos = 0;
  int seen = 0;
  while(new_pos < new_text.size() && seen < non_space) {
    if(!new_text.at(new_pos).isSpace()) {
      ++seen;
    }
    ++new_pos;
  }
  if(after_space) {
    // salta il separatore (spazi o a capo) che segue la parola precedente
    while(new_pos < new_text.size() && new_text.at(new_pos).isSpace()) {
      ++new_pos;
      if(new_text.at(new_pos - 1) == QLatin1Char('\n')) {
        break;
      }
    }
  }

  reflowing_ = true;
  QTextCursor edit(document());
  edit.joinPreviousEditBlock();
  edit.setPosition(para_start);
  edit.setPosition(para_end, QTextCursor::KeepAnchor);
  edit.insertText(new_text);
  edit.endEditBlock();
  QTextCursor place(document());
  place.setPosition(std::min(para_start + new_pos, document()->characterCount() - 1));
  setTextCursor(place);
  reflowing_ = false;
}

void LineLimitedPlainTextEdit::paintEvent(QPaintEvent* event) {
  QPlainTextEdit::paintEvent(event);
  if(max_line_length_ <= 0) {
    return;
  }

  QTextCursor cursor(document());
  cursor.movePosition(QTextCursor::Start);
  const int left = cursorRect(cursor).left();
  const QFontMetrics metrics(font());
  const int x = left + max_line_length_ * char_column_width(metrics);

  QPainter painter(viewport());
  painter.setRenderHint(QPainter::Antialiasing, false);
  QPen pen(ruler_exceeded_ ? kRulerExceededColor : kRulerOkColor, 2, Qt::SolidLine);
  painter.setPen(pen);
  painter.drawLine(x, 0, x, viewport()->height());
}

void LineLimitedPlainTextEdit::resizeEvent(QResizeEvent* event) {
  QPlainTextEdit::resizeEvent(event);
  viewport()->update();
}

void configure_mud_text_field(LineLimitedPlainTextEdit* field) {
  if(field == nullptr) {
    return;
  }
  field->setFont(QFont(QStringLiteral("Courier New"), 10));
  field->setLineWrapMode(QPlainTextEdit::NoWrap);
  field->setTabStopDistance(4 * char_column_width(QFontMetrics(field->font())));
}
