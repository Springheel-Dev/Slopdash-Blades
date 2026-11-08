"""The exe's entry: the window, or (with arguments) the command line."""
import sys

if len(sys.argv) > 1:
    from slopdash_importer.cli import main
    sys.exit(main())
from slopdash_importer.gui import main

main()
