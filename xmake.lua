add_rules("mode.debug", "mode.release")

target("poly")
set_languages("c++23")
    set_kind("shared")
    add_files("src/*.cpp")
    add_syslinks("user32", "gdi32", "comctl32", "comdlg32", "shell32", "ole32")
