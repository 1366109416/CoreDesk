#include "SearchWidget.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace coredesk::ui {

SearchWidget::SearchWidget(QWidget* parent)
    : QWidget(parent)
    , search_input_(new QLineEdit(this))
    , results_table_(new QTableWidget(this))
    , debounce_timer_(new QTimer(this))
{
    search_input_->setObjectName(QStringLiteral("searchInput"));
    search_input_->setPlaceholderText(QStringLiteral("Search"));

    debounce_timer_->setSingleShot(true);
    debounce_timer_->setInterval(150);

    results_table_->setObjectName(QStringLiteral("resultsTable"));
    results_table_->setColumnCount(4);
    results_table_->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Relative Path"), QStringLiteral("Size"), QStringLiteral("Modified")});
    results_table_->horizontalHeader()->setStretchLastSection(true);
    results_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    results_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_table_->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(search_input_);
    layout->addWidget(results_table_);
    setLayout(layout);

    QObject::connect(search_input_, &QLineEdit::textChanged, this, [this](const QString& text) {
        const auto query = text.trimmed();
        if (query_changed_callback_) {
            query_changed_callback_(query);
        }
        clear_results();
        debounce_timer_->start();
    });
    QObject::connect(debounce_timer_, &QTimer::timeout, this, [this]() {
        emit_debounced_search();
    });
}

void SearchWidget::set_search_requested_callback(SearchRequestedCallback callback)
{
    search_requested_callback_ = std::move(callback);
}

void SearchWidget::set_query_changed_callback(QueryChangedCallback callback)
{
    query_changed_callback_ = std::move(callback);
}

void SearchWidget::set_query_text(const QString& text)
{
    search_input_->setText(text);
}

QString SearchWidget::query_text() const
{
    return search_input_->text();
}

void SearchWidget::clear_results()
{
    results_table_->setRowCount(0);
}

void SearchWidget::render_results(const protocol::SearchResponsePayload& response)
{
    const auto rows = std::min<std::size_t>(response.results.size(), 100);
    results_table_->setRowCount(static_cast<int>(rows));
    for (std::size_t i = 0; i < rows; ++i) {
        const auto& result = response.results[i];
        const auto row = static_cast<int>(i);
        results_table_->setItem(row, 0, new QTableWidgetItem(QString::fromUtf8(result.name)));
        results_table_->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8(result.relative_path)));
        results_table_->setItem(row, 2, new QTableWidgetItem(format_size(result.size)));
        results_table_->setItem(row, 3, new QTableWidgetItem(format_modified(result.modified_ms)));
    }
}

int SearchWidget::result_row_count() const
{
    return results_table_->rowCount();
}

QString SearchWidget::result_name_at(int row) const
{
    const auto* item = results_table_->item(row, 0);
    return item ? item->text() : QString{};
}

void SearchWidget::emit_debounced_search()
{
    const auto query = search_input_->text().trimmed();
    if (query.isEmpty()) {
        clear_results();
        return;
    }
    if (search_requested_callback_) {
        search_requested_callback_(query);
    }
}

QString SearchWidget::format_size(std::uint64_t size) const
{
    return QString::number(size);
}

QString SearchWidget::format_modified(std::int64_t modified_ms) const
{
    if (modified_ms <= 0) {
        return QString{};
    }
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(modified_ms)).toString(Qt::ISODate);
}

} // namespace coredesk::ui
