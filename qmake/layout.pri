# Keep generated compiler and Qt files inside the selected shadow build.
OMAPIXEL_OUTPUT_DIR = $$clean_path($$OUT_PWD)
OMAPIXEL_PROJECT_DIR = $$clean_path($$OMAPIXEL_PROJECT_DIR)
OMAPIXEL_SOURCE_ROOT = $$clean_path($$OMAPIXEL_SOURCE_ROOT)

equals(OMAPIXEL_OUTPUT_DIR, $$OMAPIXEL_SOURCE_ROOT)|equals(OMAPIXEL_OUTPUT_DIR, $$OMAPIXEL_PROJECT_DIR) {
    error("Building in the source tree is not supported. Use the matching mise task.")
}

OBJECTS_DIR = $$OUT_PWD/.qmake/obj
MOC_DIR = $$OUT_PWD/.qmake/moc
RCC_DIR = $$OUT_PWD/.qmake/rcc
UI_DIR = $$OUT_PWD/.qmake/ui
