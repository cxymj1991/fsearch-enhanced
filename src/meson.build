resources = gnome.compile_resources('ui_resources',
    'gresource.xml',
    source_dir: '.',
    c_name: 'fsearch_ui',
)

resources += gnome.compile_resources('icon_resources',
    join_paths(meson.source_root(), 'data', 'gresource.xml'),
    source_dir: join_paths(meson.source_root(), 'data'),
    c_name: 'fsearch_icons',
)

libfsearch_sources = [
    resources,
    'fsearch.c',
    'fsearch_array.c',
    'fsearch_global_hotkey.c',
    'fsearch_desktop_shortcut.c',
    'fsearch_drag_drop.c',
    'fsearch_clipboard.c',
    'fsearch_config.c',
    'fsearch_database.c',
    'fsearch_database_chunked_array.c',
    'fsearch_database_entry.c',
    'fsearch_database_entry_info.c',
    'fsearch_database_exclude.c',
    'fsearch_database_exclude_manager.c',
    'fsearch_database_file.c',
    'fsearch_database_include.c',
    'fsearch_database_include_manager.c',
    'fsearch_database_index.c',
    'fsearch_database_index_event.c',
    'fsearch_database_index_store.c',
    'fsearch_database_info.c',
    'fsearch_database_preferences_widget.c',
    'fsearch_database_rescan_manager.c',
    'fsearch_database_scan.c',
    'fsearch_database_search_info.c',
    'fsearch_database_search_view.c',
    'fsearch_database_sort.c',
    'fsearch_database_work.c',
    'fsearch_file_utils.c',
    'fsearch_filter.c',
    'fsearch_filter_editor.c',
    'fsearch_filter_manager.c',
    'fsearch_filter_preferences_widget.c',
    'fsearch_folder_monitor_event.c',
    'fsearch_list_view.c',
    'fsearch_listview_popup.c',
    'fsearch_main_context_utils.c',
    'fsearch_preferences_dialog.c',
    'fsearch_query.c',
    'fsearch_query_match_data.c',
    'fsearch_query_matchers.c',
    'fsearch_query_node.c',
    'fsearch_query_lexer.c',
    'fsearch_query_parser.c',
    'fsearch_query_tree.c',
    'fsearch_result_view.c',
    'fsearch_selection.c',
    'fsearch_size_utils.c',
    'fsearch_statusbar.c',
    'fsearch_string_utils.c',
    'fsearch_time_utils.c',
    'fsearch_ui_utils.c',
    'fsearch_utf.c',
    'fsearch_window.c',
    'fsearch_window_actions.c',
    'fsearch_batch_rename.c',
    'fsearch_preview.c',
    'fsearch_tray.c',
    'fsearch_autostart.c',
    'fsearch_context_menu.c',
]

if have_inotify
    libfsearch_sources += 'fsearch_folder_monitor_inotify.c'
endif

if have_fanotify
    libfsearch_sources += 'fsearch_folder_monitor_fanotify.c'
endif

fsearch_deps = [
    cc.find_library('m', required: true),
    # 0.3.2 上游要求 GLib >= 2.62（用到 g_file_query_default_handler_async 等新 API）。
    # 统信 UOS 20（Debian 10）自带 GLib 2.58.3，无法满足；因此这里把下限调回 2.58，
    # 并由 uos20-build/patch_legacy_glib.sh 在编译前为 0.3.2 新引入的 2.60+ API 打兼容补丁
    # （当前补丁项：g_ptr_array_copy → 手写循环、g_clear_signal_handler → 展开为 disconnect+置零）。
    # 在 GLib >= 2.62 的机器（如 Debian 11/UOS 20 之外的较新发行版）上同样可正常编译，无需补丁。
    dependency('gio-unix-2.0', version: '>= 2.58'),
    dependency('gtk+-3.0', version: '>= 3.22'),
    dependency('libpcre2-8', version: '>= 10.21'),
    dependency('icu-uc', version: '>= 4.4'),
    # 全局快捷键通过 X11 XGrabKey 实现，构建期需要 libx11 开发包；
    # 目标机的运行时库 libX11.so 由打包脚本 copy_deps 收集进 lib/，离线可用。
    dependency('x11'),
]

# 系统托盘使用 GTK 自带的 GtkStatusIcon（gtk+-3.0 已包含），无需 libappindicator。

fsearch_enums_headers = [
    'fsearch_result.h'
]

fsearch_enums = gnome.mkenums_simple('fsearch_enums',
                                     sources : fsearch_enums_headers,
)

libfsearch = static_library(
    'fsearch',
    libfsearch_sources + fsearch_enums,
    dependencies: fsearch_deps,
    include_directories: fsearch_include_dirs,
)

libfsearch_include_dirs = include_directories('.')

libfsearch_dep = declare_dependency(
    link_with: libfsearch,
    include_directories: [
        fsearch_include_dirs,
        libfsearch_include_dirs
    ],
    dependencies: fsearch_deps,
    sources: resources
)

fsearch = executable('fsearch', 'main.c',
    include_directories: fsearch_include_dirs,
    dependencies: libfsearch_dep,
    install: true,
)

subdir('tests')
