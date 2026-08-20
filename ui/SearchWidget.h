#pragma once

#include "coredesk/protocol/JsonPayload.h"

#include <QWidget>

#include <functional>

class QLineEdit;
class QTableWidget;
class QTimer;

namespace coredesk::ui {

class SearchWidget : public QWidget {
public:
    using SearchRequestedCallback = std::function<void(const QString&)>;
    using QueryChangedCallback = std::function<void(const QString&)>;

    explicit SearchWidget(QWidget* parent = nullptr);

    void set_search_requested_callback(SearchRequestedCallback callback);
    void set_query_changed_callback(QueryChangedCallback callback);
    void set_query_text(const QString& text);
    QString query_text() const;
    void clear_results();
    void render_results(const protocol::SearchResponsePayload& response);
    int result_row_count() const;
    QString result_name_at(int row) const;

private:
    void emit_debounced_search();
    QString format_size(std::uint64_t size) const;
    QString format_modified(std::int64_t modified_ms) const;

    QLineEdit* search_input_{};
    QTableWidget* results_table_{};
    QTimer* debounce_timer_{};
    SearchRequestedCallback search_requested_callback_;
    QueryChangedCallback query_changed_callback_;
};

} // namespace coredesk::ui
