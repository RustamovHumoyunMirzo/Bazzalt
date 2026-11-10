"""Tag identifier validation shared by declaration parsing and inspector controls."""
def IsValidTagName(name):
    if not isinstance(name,str) or not name or name.startswith(' ') or name.endswith(' '):
        return False
    try:
        if len(name.encode('utf-8'))>64:return False
    except UnicodeEncodeError:
        return False
    return ',' not in name and not any(ord(c)<32 or ord(c)==127 for c in name)
