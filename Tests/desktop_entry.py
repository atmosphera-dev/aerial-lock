"""Literal argv encoding for fixture desktop-entry Exec values."""


def exec_argument(value):
    # First quote the argument, then apply the desktop string escape layer.
    # Field-code escaping is independent; the real %u remains a separate arg.
    value = str(value).replace("%", "%%")
    quoted = '"' + ''.join('\\' + c if c in '\\"`$' else c for c in value) + '"'
    return quoted.replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t')
