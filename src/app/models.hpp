#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QVector>

// Service / debug log feed backing the QML LogPanel TableView.
class LogModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString filterLevel READ filterLevel WRITE setFilterLevel NOTIFY filterChanged)
  Q_PROPERTY(int infoCount READ infoCount NOTIFY statsChanged)
  Q_PROPERTY(int warnCount READ warnCount NOTIFY statsChanged)
  Q_PROPERTY(int errorCount READ errorCount NOTIFY statsChanged)
  Q_PROPERTY(int debugCount READ debugCount NOTIFY statsChanged)

public:
  struct Entry {
    QString time;
    QString level;
    QString module;
    QString message;
  };

  enum Roles { TimeRole = Qt::UserRole + 1, LevelRole, ModuleRole, MessageRole };

  explicit LogModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

  int rowCount(const QModelIndex &parent = {}) const override {
    return parent.isValid() ? 0 : filtered_.size();
  }

  QVariant data(const QModelIndex &index, int role) const override {
    if (!index.isValid() || index.row() < 0 || index.row() >= filtered_.size()) return {};
    const Entry &e = entries_[filtered_[index.row()]];
    switch (role) {
      case TimeRole: return e.time;
      case LevelRole: return e.level;
      case ModuleRole: return e.module;
      case MessageRole: return e.message;
      default: return {};
    }
  }

  QHash<int, QByteArray> roleNames() const override {
    return {{TimeRole, "time"}, {LevelRole, "level"}, {ModuleRole, "module"}, {MessageRole, "message"}};
  }

  QString filterLevel() const { return filter_; }
  void setFilterLevel(const QString &level) {
    if (filter_ == level) return;
    filter_ = level;
    rebuildFiltered();
    emit filterChanged();
  }

  int infoCount() const { return info_; }
  int warnCount() const { return warn_; }
  int errorCount() const { return error_; }
  int debugCount() const { return debug_; }

  void append(const QString &level, const QString &module, const QString &message) {
    Entry e{QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), level, module, message};
    entries_.append(e);
    if (entries_.size() > 5000) entries_.removeFirst();
    if (level == "ERROR") ++error_;
    else if (level == "WARN") ++warn_;
    else if (level == "DEBUG") ++debug_;
    else ++info_;
    emit statsChanged();
    if (matches(level)) {
      const int row = filtered_.size();
      beginInsertRows({}, row, row);
      filtered_.append(entries_.size() - 1);
      endInsertRows();
    } else if (entries_.size() > 5000) {
      rebuildFiltered();
    }
  }

  Q_INVOKABLE void clear() {
    beginResetModel();
    entries_.clear();
    filtered_.clear();
    info_ = warn_ = error_ = debug_ = 0;
    endResetModel();
    emit statsChanged();
  }

  Q_INVOKABLE bool exportTsv(const QString &path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);
    out << "Time\tLevel\tModule\tInfo\n";
    for (const Entry &e : entries_)
      out << e.time << '\t' << e.level << '\t' << e.module << '\t' << e.message << '\n';
    return true;
  }

signals:
  void filterChanged();
  void statsChanged();

private:
  bool matches(const QString &level) const { return filter_.isEmpty() || filter_ == "全部" || filter_ == level; }

  void rebuildFiltered() {
    beginResetModel();
    filtered_.clear();
    for (int i = 0; i < entries_.size(); ++i)
      if (matches(entries_[i].level)) filtered_.append(i);
    endResetModel();
  }

  QVector<Entry> entries_;
  QVector<int> filtered_;
  QString filter_ = "全部";
  int info_ = 0, warn_ = 0, error_ = 0, debug_ = 0;
};

// Available overlay strategies backing the right sidebar strategy TableView.
class StrategyModel : public QAbstractListModel {
  Q_OBJECT

public:
  struct Entry {
    QString name;
    QString type;
    QString time;
  };

  enum Roles { NameRole = Qt::UserRole + 1, TypeRole, TimeRole };

  explicit StrategyModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

  int rowCount(const QModelIndex &parent = {}) const override {
    return parent.isValid() ? 0 : entries_.size();
  }

  QVariant data(const QModelIndex &index, int role) const override {
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) return {};
    const Entry &e = entries_[index.row()];
    switch (role) {
      case NameRole: return e.name;
      case TypeRole: return e.type;
      case TimeRole: return e.time;
      default: return {};
    }
  }

  QHash<int, QByteArray> roleNames() const override {
    return {{NameRole, "name"}, {TypeRole, "type"}, {TimeRole, "time"}};
  }

  void setStrategies(const QStringList &names) {
    beginResetModel();
    entries_.clear();
    for (const QString &name : names) entries_.append({name, "趋势跟随", "--"});
    endResetModel();
  }

  Q_INVOKABLE QString nameAt(int row) const {
    return (row >= 0 && row < entries_.size()) ? entries_[row].name : QString();
  }

  Q_INVOKABLE int indexOf(const QString &name) const {
    for (int i = 0; i < entries_.size(); ++i)
      if (entries_[i].name.compare(name, Qt::CaseInsensitive) == 0) return i;
    return -1;
  }

private:
  QVector<Entry> entries_;
};
