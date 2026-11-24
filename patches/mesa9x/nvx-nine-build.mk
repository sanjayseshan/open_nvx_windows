# Loaded after upstream Makefile so all original definitions are retained.
# Match the Mesa ICD Gallium enum layout.
override DEFS += -DHAVE_FUNC_ATTRIBUTE_PACKED=1
