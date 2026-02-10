import re

def strip_comments(text):
    def replacer(match):
        s = match.group(0)
        if s.startswith('/'):
            # It's a comment, return empty string or newline to preserve lines?
            # User wants "remove all the comments", so removing them entirely.
            # But line comments should remove up to newline.
            return " " if s.startswith("/*") else "" 
        else:
            return s
    
    pattern = re.compile(
        r'//.*?$|/\*.*?\*/|\'(?:\\.|[^\\\'])*\'|"(?:\\.|[^\\"])*"',
        re.DOTALL | re.MULTILINE
    )
    return re.sub(pattern, replacer, text)

# Read file
try:
    with open('src/main.cpp', 'r') as f:
        content = f.read()
    
    new_content = strip_comments(content)
    
    # Remove empty lines left by full-line comments
    lines = new_content.split('\n')
    cleaned_lines = [line for line in lines if line.strip()]
    final_content = '\n'.join(cleaned_lines)
    
    with open('src/main.cpp', 'w') as f:
        f.write(final_content)
    
    print("Comments stripped successfully.")
except Exception as e:
    print(f"Error: {e}")
