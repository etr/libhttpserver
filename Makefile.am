# not a GNU package. You can remove this line, if
# have all needed files, that a GNU package needs
AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS = src $(PYTHON_DIR) $(JAVA_DIR) $(PHP_DIR)
DIST_SUBDIRS = src $(PYTHON_DIR) $(JAVA_DIR) $(PHP_DIR)

if PYTHON
PYTHON_DIR = src/python
endif

if JAVA
JAVA_DIR = src/java
endif

if PHP
PHP_DIR = src/php
endif
