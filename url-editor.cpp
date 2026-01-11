#include <gtkmm/application.h>
#include <gtkmm/window.h>
#include <gtkmm/listbox.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/entry.h>
#include <gtkmm/statusbar.h>
#include <gtkmm/progressbar.h>
#include <gtkmm/image.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/menu.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/linkbutton.h>
#include <gtkmm/clipboard.h>
#include <glibmm/ustring.h>
#include <glibmm/fileutils.h>
#include <glibmm/convert.h>
#include <glibmm/main.h>
#include <glibmm/iochannel.h>
#include <glibmm/markup.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <regex>
#include <algorithm>

struct UrlEntry {
    Glib::ustring title;
    Glib::ustring url;

    UrlEntry(const Glib::ustring& t, const Glib::ustring& u) : title(t), url(u) {}
};

// Custom row widget for list items
class UrlRow : public Gtk::Box {
public:
    UrlRow(const Glib::ustring& title, const Glib::ustring& url)
        : Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10), title_text(title), url_text(url) {
        set_margin_start(5);
        set_margin_end(5);
        set_margin_top(5);
        set_margin_bottom(5);

        // Create number label
        number_label = Gtk::manage(new Gtk::Label("0"));
        number_label->set_halign(Gtk::ALIGN_START);
        number_label->set_valign(Gtk::ALIGN_START);
        number_label->set_width_chars(4);
        number_label->set_margin_end(10);
        pack_start(*number_label, false, false);

        // Create icon
        icon_image = Gtk::manage(new Gtk::Image());
        icon_image->set_size_request(32, 32);
        pack_start(*icon_image, false, false);

        // Create vertical box for title and URL
        Gtk::Box* text_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));

        // Create title label
        title_label = Gtk::manage(new Gtk::Label(title));
        title_label->set_halign(Gtk::ALIGN_START);
        title_label->set_valign(Gtk::ALIGN_START);
        title_label->set_ellipsize(Pango::ELLIPSIZE_END);
        text_box->pack_start(*title_label, false, false);

        // Create URL label (blue and underlined, but not clickable)
        url_label = Gtk::manage(new Gtk::Label());
        url_label->set_halign(Gtk::ALIGN_START);
        url_label->set_valign(Gtk::ALIGN_START);
        url_label->set_line_wrap(true);
        url_label->set_line_wrap_mode(Pango::WRAP_WORD);
        url_label->set_selectable(false);
        url_label->set_max_width_chars(80); // Allow wrapping at reasonable width

        // Make it look like a link (blue and underlined via markup)
        Glib::ustring url_markup = "<span underline=\"single\" color=\"#0000FF\">" +
                                   Glib::Markup::escape_text(url) + "</span>";
        url_label->set_markup(url_markup);

        text_box->pack_start(*url_label, false, false);

        pack_start(*text_box, true, true);

        show_all();
    }

    void set_icon(Glib::RefPtr<Gdk::Pixbuf> pixbuf) {
        if (pixbuf) {
            icon_image->set(pixbuf->scale_simple(32, 32, Gdk::INTERP_BILINEAR));
        }
    }

    void set_number(int number) {
        number_label->set_text(Glib::ustring::compose("%1.", number));
    }

    Glib::ustring get_url() const { return url_text; }
    Glib::ustring get_title() const { return title_text; }

private:
    Gtk::Label* number_label;
    Gtk::Image* icon_image;
    Gtk::Label* title_label;
    Gtk::Label* url_label;
    Glib::ustring title_text;
    Glib::ustring url_text;
};

class UrlEditorWindow : public Gtk::Window {
public:
    UrlEditorWindow() {
        set_title("URL Editor");
        set_default_size(900, 1000); // WxH
        set_border_width(10);

        // Create main box
        main_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 10));
        add(*main_box);

        // Create header box with file path entry and URL count
        header_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));

        // File path entry
        Gtk::Label* path_label = Gtk::manage(new Gtk::Label("File path:"));
        path_label->set_halign(Gtk::ALIGN_START);
        header_box->pack_start(*path_label, false, false);

        file_path_entry = Gtk::manage(new Gtk::Entry());
        file_path_entry->set_text("urls.txt");
        file_path_entry->set_hexpand(true);
        file_path_entry->set_tooltip_text("Enter the path to the URLs file");
        header_box->pack_start(*file_path_entry, true, true);

        url_count_label = Gtk::manage(new Gtk::Label("URLs: 0"));
        url_count_label->set_halign(Gtk::ALIGN_START);
        url_count_label->set_margin_start(10);
        header_box->pack_start(*url_count_label, false, false);

        main_box->pack_start(*header_box, false, false);

        // Create scrolled window for list
        scrolled_window = Gtk::manage(new Gtk::ScrolledWindow());
        scrolled_window->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
        scrolled_window->set_vexpand(true);

        // Create list box
        list_box = Gtk::manage(new Gtk::ListBox());
        list_box->set_selection_mode(Gtk::SELECTION_SINGLE);
        scrolled_window->add(*list_box);
        main_box->pack_start(*scrolled_window, true, true);

        // Connect signals
        list_box->signal_row_activated().connect(sigc::mem_fun(*this, &UrlEditorWindow::on_row_activated));
        list_box->signal_button_press_event().connect(sigc::mem_fun(*this, &UrlEditorWindow::on_button_press));
        list_box->signal_row_selected().connect(sigc::mem_fun(*this, &UrlEditorWindow::on_row_selected));

        // Add keyboard shortcuts - handle key press events
        signal_key_press_event().connect(sigc::mem_fun(*this, &UrlEditorWindow::on_key_press), false);

        // Create button box
        button_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));

        load_button = Gtk::manage(new Gtk::Button("Load"));
        save_button = Gtk::manage(new Gtk::Button("Save to path"));
        refresh_button = Gtk::manage(new Gtk::Button("Refresh Icons"));

        // Movement and delete buttons
        move_up_button = Gtk::manage(new Gtk::Button("↑"));
        move_down_button = Gtk::manage(new Gtk::Button("↓"));
        delete_button = Gtk::manage(new Gtk::Button("Delete"));
        copy_url_button = Gtk::manage(new Gtk::Button("Copy URL"));
        open_chromium_button = Gtk::manage(new Gtk::Button("Open in Chromium"));

        load_button->signal_clicked().connect(sigc::mem_fun(*this, &UrlEditorWindow::on_load_clicked));
        save_button->signal_clicked().connect(sigc::mem_fun(*this, &UrlEditorWindow::on_save_clicked));
        refresh_button->signal_clicked().connect(sigc::mem_fun(*this, &UrlEditorWindow::on_refresh_clicked));
        move_up_button->signal_clicked().connect(sigc::mem_fun(*this, &UrlEditorWindow::on_move_up_clicked));
        move_down_button->signal_clicked().connect(sigc::mem_fun(*this, &UrlEditorWindow::on_move_down_clicked));
        delete_button->signal_clicked().connect(sigc::mem_fun(*this, &UrlEditorWindow::on_delete_clicked));
        copy_url_button->signal_clicked().connect(sigc::mem_fun(*this, &UrlEditorWindow::on_copy_url_clicked));
        open_chromium_button->signal_clicked().connect(sigc::mem_fun(*this, &UrlEditorWindow::on_open_chromium_clicked));

        button_box->pack_start(*load_button, false, false);
        button_box->pack_start(*save_button, false, false);
        button_box->pack_start(*refresh_button, false, false);
        button_box->pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL)), false, false);
        button_box->pack_start(*move_up_button, false, false);
        button_box->pack_start(*move_down_button, false, false);
        button_box->pack_start(*delete_button, false, false);
        button_box->pack_start(*copy_url_button, false, false);
        button_box->pack_start(*open_chromium_button, false, false);
        button_box->pack_end(*Gtk::manage(new Gtk::Label()), true, true);

        // Initially disable movement buttons (no selection)
        move_up_button->set_sensitive(false);
        move_down_button->set_sensitive(false);
        delete_button->set_sensitive(false);
        copy_url_button->set_sensitive(false);

        main_box->pack_start(*button_box, false, false);

        // Create status bar
        status_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
        status_label = Gtk::manage(new Gtk::Label("Ready"));
        status_label->set_halign(Gtk::ALIGN_START);
        status_box->pack_start(*status_label, true, true);

        progress_bar = Gtk::manage(new Gtk::ProgressBar());
        progress_bar->set_visible(false);
        status_box->pack_end(*progress_bar, false, false);

        main_box->pack_start(*status_box, false, false);

        show_all();

        // Initialize curl
        curl_global_init(CURL_GLOBAL_DEFAULT);

        // Load URLs on startup
        load_urls();
    }

    ~UrlEditorWindow() {
        curl_global_cleanup();
    }

private:
    void on_row_selected(Gtk::ListBoxRow* row) {
        // Store the currently selected row
        current_selected_row = row;
        // This is called when selection changes via user interaction
        // But we also call update_button_states manually after moves
        update_button_states(row);
    }

    void on_move_up_clicked() {
        // Try to get selected row - use stored reference if get_selected_row() fails
        Gtk::ListBoxRow* selected_row = list_box->get_selected_row();
        if (!selected_row) {
            selected_row = current_selected_row;
        }
        if (!selected_row) return;

        int current_index = selected_row->get_index();
        if (current_index <= 0) return; // Already at top

        // Get all children
        std::vector<Gtk::Widget*> children = list_box->get_children();
        if (current_index > 0 && current_index < (int)children.size()) {
            // Get the widget to move (the selected row)
            Gtk::Widget* widget_to_move = children[current_index];
            Gtk::ListBoxRow* row_to_move = dynamic_cast<Gtk::ListBoxRow*>(widget_to_move);
            if (!row_to_move) return;

            // Remove it from current position
            list_box->remove(*widget_to_move);

            // Insert at new position (one position up)
            int new_index = current_index - 1;
            list_box->insert(*widget_to_move, new_index);

            // Ensure it's visible
            list_box->show_all();

            // Re-select by getting the row at the new index
            // This ensures we have the correct row reference
            Gtk::ListBoxRow* row_at_new_index = list_box->get_row_at_index(new_index);
            Gtk::ListBoxRow* row_to_select = row_at_new_index ? row_at_new_index : row_to_move;

            if (row_to_select) {
                list_box->select_row(*row_to_select);
                row_to_select->grab_focus();
                // Update our stored reference
                current_selected_row = row_to_select;
                update_button_states(row_to_select);
            }

            // Update row numbers after move
            update_row_numbers();

            // Use idle callback to ensure selection is properly maintained
            // This processes pending events and ensures GTK updates its internal state
            Glib::signal_idle().connect_once([this, new_index, row_to_select]() {
                Gtk::ListBoxRow* row = list_box->get_row_at_index(new_index);
                if (row) {
                    list_box->select_row(*row);
                    current_selected_row = row;
                    update_button_states(row);
                } else if (row_to_select) {
                    // Fallback to the row we selected earlier
                    list_box->select_row(*row_to_select);
                    current_selected_row = row_to_select;
                    update_button_states(row_to_select);
                }
            });
        }
    }

    void on_move_down_clicked() {
        // Try to get selected row - use stored reference if get_selected_row() fails
        Gtk::ListBoxRow* selected_row = list_box->get_selected_row();
        if (!selected_row) {
            selected_row = current_selected_row;
        }
        if (!selected_row) return;

        int current_index = selected_row->get_index();
        std::vector<Gtk::Widget*> children = list_box->get_children();

        if (current_index >= 0 && current_index < (int)children.size() - 1) {
            // Get the widget to move (the selected row)
            Gtk::Widget* widget_to_move = children[current_index];
            Gtk::ListBoxRow* row_to_move = dynamic_cast<Gtk::ListBoxRow*>(widget_to_move);
            if (!row_to_move) return;

            // Remove it from current position
            list_box->remove(*widget_to_move);

            // Insert at new position (one position down)
            int new_index = current_index + 1;
            list_box->insert(*widget_to_move, new_index);

            // Ensure it's visible
            list_box->show_all();

            // Re-select by getting the row at the new index
            // This ensures we have the correct row reference
            Gtk::ListBoxRow* row_at_new_index = list_box->get_row_at_index(new_index);
            Gtk::ListBoxRow* row_to_select = row_at_new_index ? row_at_new_index : row_to_move;

            if (row_to_select) {
                list_box->select_row(*row_to_select);
                row_to_select->grab_focus();
                // Update our stored reference
                current_selected_row = row_to_select;
                update_button_states(row_to_select);
            }

            // Update row numbers after move
            update_row_numbers();

            // Use idle callback to ensure selection is properly maintained
            // This processes pending events and ensures GTK updates its internal state
            Glib::signal_idle().connect_once([this, new_index, row_to_select]() {
                Gtk::ListBoxRow* row = list_box->get_row_at_index(new_index);
                if (row) {
                    list_box->select_row(*row);
                    current_selected_row = row;
                    update_button_states(row);
                } else if (row_to_select) {
                    // Fallback to the row we selected earlier
                    list_box->select_row(*row_to_select);
                    current_selected_row = row_to_select;
                    update_button_states(row_to_select);
                }
            });
        }
    }

    void update_button_states(Gtk::ListBoxRow* row) {
        if (!row) {
            move_up_button->set_sensitive(false);
            move_down_button->set_sensitive(false);
            delete_button->set_sensitive(false);
            copy_url_button->set_sensitive(false);
            open_chromium_button->set_sensitive(false);
            return;
        }

        int index = row->get_index();
        std::vector<Gtk::Widget*> children = list_box->get_children();
        int total_items = children.size();

        delete_button->set_sensitive(true);
        copy_url_button->set_sensitive(true);
        open_chromium_button->set_sensitive(true);
        move_up_button->set_sensitive(index > 0);
        move_down_button->set_sensitive(index >= 0 && index < total_items - 1);
    }

    void update_url_count() {
        std::vector<Gtk::Widget*> children = list_box->get_children();
        int count = children.size();
        url_count_label->set_text(Glib::ustring::compose("URLs: %1", count));
    }

    void update_row_numbers() {
        std::vector<Gtk::Widget*> children = list_box->get_children();
        for (size_t i = 0; i < children.size(); ++i) {
            Gtk::ListBoxRow* row = dynamic_cast<Gtk::ListBoxRow*>(children[i]);
            if (row) {
                UrlRow* url_row = dynamic_cast<UrlRow*>(row->get_child());
                if (url_row) {
                    url_row->set_number(i + 1);
                }
            }
        }
    }

    void on_copy_url_clicked() {
        Gtk::ListBoxRow* row = list_box->get_selected_row();
        if (!row) {
            row = current_selected_row;
        }
        if (!row) return;

        UrlRow* url_row = dynamic_cast<UrlRow*>(row->get_child());
        if (!url_row) return;

        Glib::ustring url = url_row->get_url();
        if (url.empty()) return;

        // Get the default clipboard
        Glib::RefPtr<Gtk::Clipboard> clipboard = Gtk::Clipboard::get(GDK_SELECTION_CLIPBOARD);
        clipboard->set_text(url);

        // Also set to primary selection (middle-click paste on Linux)
        Glib::RefPtr<Gtk::Clipboard> primary = Gtk::Clipboard::get(GDK_SELECTION_PRIMARY);
        primary->set_text(url);

        status_label->set_text("URL copied to clipboard");
    }

    void on_open_chromium_clicked() {
        Gtk::ListBoxRow* row = list_box->get_selected_row();
        if (!row) {
            row = current_selected_row;
        }
        if (!row) return;

        UrlRow* url_row = dynamic_cast<UrlRow*>(row->get_child());
        if (!url_row) return;

        Glib::ustring url = url_row->get_url();
        if (url.empty()) return;

        std::string url_str = url.raw();

        // Ensure URL has a scheme
        if (url_str.find("://") == std::string::npos) {
            url_str = "http://" + url_str;
        }

        // Use the specific Chromium path
        const char* chromium_path = "/home/elias/.local/bin/ungoogled-chromium.AppImage";

        // Build command: chromium_path --new-tab url
        std::string command = std::string(chromium_path) + " --new-tab \"" + url_str + "\"";

        GError* error = nullptr;
        if (!g_spawn_command_line_async(command.c_str(), &error)) {
            status_label->set_text("Failed to open URL in Chromium");
            if (error) {
                g_warning("Failed to launch Chromium: %s", error->message);
                g_error_free(error);
            }
        } else {
            status_label->set_text("Opening URL in Chromium...");
        }
    }

    void on_delete_clicked() {
        Gtk::ListBoxRow* row = list_box->get_selected_row();
        if (!row) return;

        int index = row->get_index();
        std::vector<Gtk::Widget*> children = list_box->get_children();

        if (index >= 0 && index < (int)children.size()) {
            Gtk::Widget* widget = children[index];
            list_box->remove(*widget);

            // Select next item if available, or previous if at end
            if (index < (int)children.size() - 1) {
                // Select the item that moved into this position
                children = list_box->get_children();
                if (index < (int)children.size()) {
                    Gtk::ListBoxRow* new_selection = dynamic_cast<Gtk::ListBoxRow*>(children[index]);
                    if (new_selection) {
                        list_box->select_row(*new_selection);
                        current_selected_row = new_selection;
                    }
                }
            } else if (index > 0) {
                // Select the previous item
                children = list_box->get_children();
                if (index - 1 < (int)children.size()) {
                    Gtk::ListBoxRow* new_selection = dynamic_cast<Gtk::ListBoxRow*>(children[index - 1]);
                    if (new_selection) {
                        list_box->select_row(*new_selection);
                        current_selected_row = new_selection;
                    }
                }
            } else {
                // No items left, disable buttons
                current_selected_row = nullptr;
                update_button_states(nullptr);
            }

            list_box->show_all();

            // Update URL count and row numbers
            update_url_count();
            update_row_numbers();
        }
    }

    void on_row_activated(Gtk::ListBoxRow* row) {
        if (row) {
            UrlRow* url_row = dynamic_cast<UrlRow*>(row->get_child());
            if (url_row) {
                open_url(url_row->get_url());
            }
        }
    }

    bool on_button_press(GdkEventButton* event) {
        if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
            // Right click
            Gtk::ListBoxRow* row = list_box->get_row_at_y((int)event->y);
            if (row) {
                show_context_menu(row, event);
                return true;
            }
        }
        return false;
    }

    bool on_key_press(GdkEventKey* event) {
        // Handle Ctrl+C to copy URL
        if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_c) {
            on_copy_url_clicked();
            return true;
        }

        // Handle Delete key to delete entry
        if (event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_BackSpace) {
            on_delete_clicked();
            return true;
        }

        // Handle Enter key to open in Chromium
        if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
            on_open_chromium_clicked();
            return true;
        }

        return false;
    }

    void show_context_menu(Gtk::ListBoxRow* row, GdkEventButton* event) {
        UrlRow* url_row = dynamic_cast<UrlRow*>(row->get_child());
        if (!url_row) return;

        Gtk::Menu* menu = Gtk::manage(new Gtk::Menu());
        Gtk::MenuItem* open_item = Gtk::manage(new Gtk::MenuItem("Open URL"));
        open_item->signal_activate().connect([this, url_row]() {
            open_url(url_row->get_url());
        });
        menu->append(*open_item);
        menu->show_all();
        menu->popup(event->button, gdk_event_get_time((GdkEvent*)event));
    }

    void open_url(const Glib::ustring& url) {
        if (url.empty()) return;

        GError* error = nullptr;
        std::string url_str = url.raw();

        // Ensure URL has a scheme
        if (url_str.find("://") == std::string::npos) {
            url_str = "http://" + url_str;
        }

        gchar* uri = g_strdup(url_str.c_str());

        // Use g_app_info_launch_default_for_uri which opens in default browser
        // Most modern browsers will open this in a new tab if already running
        if (!g_app_info_launch_default_for_uri(uri, nullptr, &error)) {
            g_warning("Failed to open URL: %s", error ? error->message : "Unknown error");
            if (error) {
                g_error_free(error);
            }
        }

        g_free(uri);
    }

    void on_load_clicked() {
        load_urls();
    }

    void on_save_clicked() {
        save_urls();
    }

    void on_refresh_clicked() {
        download_favicons();
    }

    std::string convert_file_uri_to_path(const Glib::ustring& uri_or_path) {
        std::string path = uri_or_path.raw();

        // Check if it starts with file://
        if (path.find("file://") == 0) {
            // Remove file:// prefix
            path = path.substr(7);

            // Handle localhost (file://localhost/path -> /path)
            if (path.find("localhost/") == 0) {
                path = path.substr(9);
            }

            // Ensure absolute path starts with /
            if (path.empty() || path[0] != '/') {
                path = "/" + path;
            }

            // Decode URL encoding if present (e.g., %20 -> space)
            // Simple approach: use Glib to decode
            try {
                gchar* decoded = g_uri_unescape_string(path.c_str(), nullptr);
                if (decoded) {
                    path = decoded;
                    g_free(decoded);
                }
            } catch (...) {
                // If decoding fails, use original path
            }
        }

        return path;
    }

    void load_urls() {
        Glib::ustring file_path = file_path_entry->get_text();
        if (file_path.empty()) {
            status_label->set_text("Error: Please enter a file path");
            return;
        }

        std::string path_str = convert_file_uri_to_path(file_path);
        // Open file in binary mode to read UTF-8 correctly
        std::ifstream file(path_str, std::ios::binary);
        if (!file.is_open()) {
            status_label->set_text(Glib::ustring::compose("Error: Could not open %1", file_path));
            return;
        }

        // Clear existing items
        std::vector<Gtk::Widget*> children = list_box->get_children();
        for (Gtk::Widget* child : children) {
            list_box->remove(*child);
        }
        url_entries.clear();

        std::string line;
        Glib::ustring current_title;
        bool expecting_url = false;

        while (std::getline(file, line)) {
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\n\r"));
            line.erase(line.find_last_not_of(" \t\n\r") + 1);

            if (line.empty()) {
                expecting_url = false;
                continue;
            }

            // Treat the line as UTF-8 (which is the standard for modern text files)
            // Glib::ustring can handle UTF-8 strings directly
            Glib::ustring ustring_line;
            try {
                // Try to create ustring from UTF-8 bytes
                // If the string is already valid UTF-8, this will work
                ustring_line = Glib::ustring(line);
            } catch (...) {
                // If that fails, try to convert from locale encoding
                try {
                    ustring_line = Glib::locale_to_utf8(line);
                } catch (...) {
                    // Last resort: use as-is (might have encoding issues)
                    ustring_line = line;
                }
            }

            if (!expecting_url) {
                current_title = ustring_line;
                expecting_url = true;
            } else {
                url_entries.push_back(UrlEntry(current_title, ustring_line));
                add_url_entry(current_title, ustring_line);
                expecting_url = false;
            }
        }

        file.close();
        status_label->set_text(Glib::ustring::compose("Loaded %1 URLs", url_entries.size()));
        list_box->show_all();

        // Update URL count and row numbers
        update_url_count();
        update_row_numbers();

        // Start downloading favicons
        download_favicons();
    }

    void save_urls() {
        Glib::ustring file_path = file_path_entry->get_text();
        if (file_path.empty()) {
            status_label->set_text("Error: Please enter a file path");
            return;
        }

        std::string path_str = convert_file_uri_to_path(file_path);
        // Open file in binary mode to preserve UTF-8 encoding
        std::ofstream file(path_str, std::ios::binary);
        if (!file.is_open()) {
            status_label->set_text(Glib::ustring::compose("Error: Could not save %1", file_path));
            return;
        }

        std::vector<Gtk::Widget*> children = list_box->get_children();
        std::vector<UrlEntry> ordered_entries;

        for (Gtk::Widget* child : children) {
            Gtk::ListBoxRow* row = dynamic_cast<Gtk::ListBoxRow*>(child);
            if (row) {
                UrlRow* url_row = dynamic_cast<UrlRow*>(row->get_child());
                if (url_row) {
                    ordered_entries.push_back(UrlEntry(url_row->get_title(), url_row->get_url()));
                }
            }
        }

        for (size_t i = 0; i < ordered_entries.size(); ++i) {
            // Write UTF-8 strings directly (raw() returns UTF-8 bytes)
            std::string title_utf8 = ordered_entries[i].title.raw();
            std::string url_utf8 = ordered_entries[i].url.raw();
            file << title_utf8 << "\n";
            file << url_utf8 << "\n";
            if (i < ordered_entries.size() - 1) {
                file << "\n";
            }
        }

        file.close();
        status_label->set_text(Glib::ustring::compose("Saved %1 URLs", ordered_entries.size()));
    }

    void add_url_entry(const Glib::ustring& title, const Glib::ustring& url) {
        UrlRow* row_widget = Gtk::manage(new UrlRow(title, url));
        Gtk::ListBoxRow* row = Gtk::manage(new Gtk::ListBoxRow());
        row->add(*row_widget);
        list_box->append(*row);

        // Update URL count and row numbers
        update_url_count();
        update_row_numbers();
    }

    void download_favicons() {
        std::vector<Gtk::Widget*> children = list_box->get_children();
        pending_downloads = children.size();
        completed_downloads = 0;

        if (pending_downloads == 0) {
            return;
        }

        progress_bar->set_visible(true);
        progress_bar->set_fraction(0.0);
        status_label->set_text("Downloading favicons...");

        int index = 0;
        for (Gtk::Widget* child : children) {
            Gtk::ListBoxRow* row = dynamic_cast<Gtk::ListBoxRow*>(child);
            if (row) {
                UrlRow* url_row = dynamic_cast<UrlRow*>(row->get_child());
                if (url_row) {
                    download_favicon_for_url(url_row->get_url(), index, 0);
                }
            }
            index++;
        }
    }

    void download_favicon_for_url(const Glib::ustring& url_string, int item_index, int attempt) {
        std::thread([this, url_string, item_index, attempt]() {
            std::string url = url_string.raw();
            std::string base_url = extract_base_url(url);
            std::string favicon_url;

            switch (attempt) {
                case 0:
                    favicon_url = base_url + "/favicon.ico";
                    break;
                case 1:
                    favicon_url = base_url + "/favicon.png";
                    break;
                case 2:
                    {
                        std::string host = extract_host(url);
                        favicon_url = "https://www.google.com/s2/favicons?domain=" + host + "&sz=32";
                    }
                    break;
                default:
                    Glib::signal_idle().connect_once([this]() { update_progress(); });
                    return;
            }

            CURL* curl = curl_easy_init();
            if (!curl) {
                Glib::signal_idle().connect_once([this]() { update_progress(); });
                return;
            }

            std::string response_data;

            curl_easy_setopt(curl, CURLOPT_URL, favicon_url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

            CURLcode res = curl_easy_perform(curl);
            long response_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            curl_easy_cleanup(curl);

            bool success = false;
            if (res == CURLE_OK && response_code == 200 && !response_data.empty()) {
                // Use C API to load pixbuf from data
                GError* error = nullptr;
                GdkPixbufLoader* loader = gdk_pixbuf_loader_new();

                if (loader) {
                    gboolean write_ok = gdk_pixbuf_loader_write(loader,
                        (const guint8*)response_data.data(),
                        response_data.size(), &error);

                    if (write_ok && !error) {
                        gboolean close_ok = gdk_pixbuf_loader_close(loader, &error);

                        if (close_ok && !error) {
                            GdkPixbuf* pixbuf_c = gdk_pixbuf_loader_get_pixbuf(loader);

                            if (pixbuf_c) {
                                // The loader owns the pixbuf, so we need to ref it to keep it alive
                                g_object_ref(pixbuf_c);
                                // Wrap it in a RefPtr - wrap() will manage the ref
                                Glib::RefPtr<Gdk::Pixbuf> pixbuf = Glib::wrap(pixbuf_c);

                                if (pixbuf) {
                                    Glib::signal_idle().connect_once([this, pixbuf, item_index]() {
                                        set_favicon(item_index, pixbuf);
                                    });
                                    success = true;
                                }
                            }
                        }
                    }

                    if (error) {
                        g_error_free(error);
                    }

                    g_object_unref(loader);
                }
            }

            if (!success && attempt < 2) {
                download_favicon_for_url(url_string, item_index, attempt + 1);
            } else if (!success) {
                // Create fallback icon
                Glib::RefPtr<Gdk::Pixbuf> fallback = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, 32, 32);
                fallback->fill(0x80808080); // Gray with alpha
                Glib::signal_idle().connect_once([this, fallback, item_index]() {
                    set_favicon(item_index, fallback);
                });
            }

            Glib::signal_idle().connect_once([this]() { update_progress(); });
        }).detach();
    }

    void set_favicon(int item_index, Glib::RefPtr<Gdk::Pixbuf> pixbuf) {
        std::vector<Gtk::Widget*> children = list_box->get_children();
        if (item_index >= 0 && item_index < (int)children.size()) {
            Gtk::ListBoxRow* row = dynamic_cast<Gtk::ListBoxRow*>(children[item_index]);
            if (row) {
                UrlRow* url_row = dynamic_cast<UrlRow*>(row->get_child());
                if (url_row) {
                    url_row->set_icon(pixbuf);
                }
            }
        }
    }

    void update_progress() {
        completed_downloads++;
        double fraction = (double)completed_downloads / pending_downloads;
        progress_bar->set_fraction(fraction);

        if (completed_downloads >= pending_downloads) {
            progress_bar->set_visible(false);
            std::vector<Gtk::Widget*> children = list_box->get_children();
            status_label->set_text(Glib::ustring::compose("Loaded %1 URLs", children.size()));
        }
    }

    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    std::string extract_base_url(const std::string& url_string) {
        std::regex url_regex(R"(^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?)");
        std::smatch match;

        if (std::regex_match(url_string, match, url_regex)) {
            std::string scheme = match[2].str();
            std::string host = match[4].str();

            if (!scheme.empty() && !host.empty()) {
                return scheme + "://" + host;
            }
        }

        return url_string;
    }

    std::string extract_host(const std::string& url_string) {
        std::regex url_regex(R"(^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?)");
        std::smatch match;

        if (std::regex_match(url_string, match, url_regex)) {
            return match[4].str();
        }

        return "";
    }

    Gtk::Box* main_box;
    Gtk::Box* header_box;
    Gtk::Entry* file_path_entry;
    Gtk::Label* url_count_label;
    Gtk::ScrolledWindow* scrolled_window;
    Gtk::ListBox* list_box;
    Gtk::Box* button_box;
    Gtk::Button* load_button;
    Gtk::Button* save_button;
    Gtk::Button* refresh_button;
    Gtk::Button* move_up_button;
    Gtk::Button* move_down_button;
    Gtk::Button* delete_button;
    Gtk::Button* copy_url_button;
    Gtk::Button* open_chromium_button;
    Gtk::Box* status_box;
    Gtk::Label* status_label;
    Gtk::ProgressBar* progress_bar;

    std::vector<UrlEntry> url_entries;
    int pending_downloads = 0;
    std::atomic<int> completed_downloads{0};
    Gtk::ListBoxRow* current_selected_row = nullptr;
};

int main(int argc, char* argv[]) {
    auto app = Gtk::Application::create(argc, argv, "com.stelijah.url-editor");

    UrlEditorWindow window;

    return app->run(window);
}
