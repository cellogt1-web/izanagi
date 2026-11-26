#pragma once

#include <QObject>
#include <QPlainTextEdit>
#include <QStringList>

/**
 * Impostazioni globali di impaginazione dei testi MUD: limite caratteri per riga
 * e giustificazione. Salvate in QSettings; chi le usa si aggiorna con changed().
 */
class MudTextFormat : public QObject {
  Q_OBJECT
 public:
  static MudTextFormat& instance();
  int maxLength() const { return max_length_; }
  bool justify() const { return justify_; }
  void set(int max_length, bool justify);

 signals:
  void changed();

 private:
  MudTextFormat();
  int max_length_ = 0;
  bool justify_ = false;
};

/**
 * Impagina un testo MUD: paragrafi separati da righe vuote; dentro un paragrafo le
 * righe vengono unite e rimandate a capo entro max_len (spazi multipli ridotti a uno).
 * Con justify le righe (tranne l'ultima di ogni paragrafo) vengono riempite di spazi.
 */
QString reflow_mud_text(const QString& text, int max_len, bool justify);

/** QPlainTextEdit con barra verticale di limite caratteri per riga (stile Izanagi). */
class LineLimitedPlainTextEdit : public QPlainTextEdit {
  Q_OBJECT
 public:
  explicit LineLimitedPlainTextEdit(QWidget* parent = nullptr);

  void setMaxLineLength(int max_length);
  int maxLineLength() const { return max_line_length_; }

  /** A capo automatico mentre si scrive e ricalcolo al cambio del limite. */
  void setAutoWrap(bool enabled) { auto_wrap_ = enabled; }
  bool autoWrap() const { return auto_wrap_; }
  /** Reimpagina tutto il testo con le impostazioni correnti. */
  void reflowAll();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void insertFromMimeData(const QMimeData* source) override;

 private:
  void refreshRulerState();
  bool anyLineExceedsLimit() const;
  void reflowCurrentParagraph();
  void onFormatChanged();

  int max_line_length_ = 0;
  bool ruler_exceeded_ = false;
  bool auto_wrap_ = false;
  bool justify_ = false;
  bool reflowing_ = false;
};

void configure_mud_text_field(LineLimitedPlainTextEdit* field);
