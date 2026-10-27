# Provenance

`src/`, `tests/` and `tools/` contain the new diagnostic application and its tooling, under MIT. No GPU-unlock core or NVIDIA firmware is linked into this application.

The shared ../../ad104-msi/vendor/edk2/Include/ tree contains unmodified EDK II
headers from TianoCore commit c1415771ac592ad8509351c46cd3cda4f38d7d52,
obtained through impacifist/Windows-GPU-UNLOCK at
9352abd27480203ea146f766e7c96957912d148a. Its headers.json pins the contents.
Retain its License.txt and individual notices. No EDK II binary is linked.

Adapted from windows/ad104-msi/diagnostic at repository commit d28c485.
The shared headers are referenced from ../../ad104-msi/vendor/edk2 and their
actual contents are verified against its headers.json during validation.
Retain that directory's EDK II licence and all source notices. No transaction
implementation or NVIDIA firmware is linked. This fork changes identity,
BAR validation, chip guards, bounded ROM export and offline analysis/tests.
