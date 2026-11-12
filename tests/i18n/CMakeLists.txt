# The English catalogue (docs/DECISIONS.md C41, #197): every `%n` string has its singular and
# plural, the embedded .qm gives them to C++ tr() and QML qsTr(), and the committed .ts holds
# exactly the `%n` strings the sources have.
omaseed_add_test(tst_translations
    SOURCES tst_translations.cpp
    LIBRARIES omaseed::i18n Qt6::Qml
    DEFINES
        OMASEED_TS_FILE="${PROJECT_SOURCE_DIR}/translations/omaseed_en.ts"
        OMASEED_TS_CHECK_FILE="${CMAKE_CURRENT_BINARY_DIR}/check/omaseed_en.ts"
)

# The sync check's own lupdate: the same sources and options as update_translations, written to
# a .ts in the build tree, never the committed one. It stays out of update_translations and of
# the build; the fixture below runs it before tst_translations reads its result.
get_target_property(translation_source_targets omaseed-i18n OMASEED_TRANSLATION_SOURCE_TARGETS)
set(check_ts "${CMAKE_CURRENT_BINARY_DIR}/check/omaseed_en.ts")
qt_add_lupdate(
    SOURCE_TARGETS ${translation_source_targets}
    PLURALS_TS_FILE "${check_ts}"
    LUPDATE_TARGET omaseed_lupdate_check
    NO_GLOBAL_TARGET
    OPTIONS -no-obsolete -locations relative -silent
)
add_test(NAME tst_translations_lupdate
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target omaseed_lupdate_check
)
set_tests_properties(tst_translations_lupdate PROPERTIES
    FIXTURES_SETUP translations_check
    LABELS "i18n"
    TIMEOUT 120
)
set_tests_properties(tst_translations PROPERTIES
    FIXTURES_REQUIRED translations_check
    LABELS "i18n"
)
