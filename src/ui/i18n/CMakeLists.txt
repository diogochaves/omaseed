# omaseed-i18n: the translations (docs/DECISIONS.md C41, #197). One English catalogue,
# translations/omaseed_en.ts, holds the singular and plural of every `%n` string and nothing
# else (Qt's plurals-only file for the source language: lupdate, then `lconvert -pluralonly`).
# lrelease compiles it at build time and the .qm is embedded under :/i18n, so nothing is
# installed beside the binary. Every other string falls back to its source text.
#
# lupdate runs only on demand: `cmake --build build --target update_translations` after adding
# or changing a `%n` string, then fill the new numerus forms (Qt Linguist, or by hand). The
# build itself never writes the .ts. tests/i18n checks the forms and that the file is in sync
# with the sources.
qt_add_library(omaseed-i18n STATIC
    Translations.cpp
    Translations.hpp
)
add_library(omaseed::i18n ALIAS omaseed-i18n)

target_include_directories(omaseed-i18n PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(omaseed-i18n
    PRIVATE omaseed::warnings
    PUBLIC Qt6::Core
)

# Every target with translatable strings, C++ and QML: lupdate reads their sources (the QML
# modules' QML files included). Listed, not collected, so the tests' strings stay out.
# tests/i18n reads the same list from the property, for its sync check.
set(translation_source_targets
    omaseed
    omaseed-app
    omaseed-core
    omaseed-integration
    omaseed-qml
    omaseed-kit
    omaseed-views
)
set_property(TARGET omaseed-i18n PROPERTY
    OMASEED_TRANSLATION_SOURCE_TARGETS ${translation_source_targets})

# -no-obsolete: a string removed from the sources leaves the file, not a vanished entry.
# -locations relative: each message's place is a line delta from the one before, so an edit
# near one string does not move every line number after it in the diff.
qt_add_translations(TARGETS omaseed-i18n
    SOURCE_TARGETS ${translation_source_targets}
    PLURALS_TS_FILE "${PROJECT_SOURCE_DIR}/translations/omaseed_en.ts"
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations relative
)
