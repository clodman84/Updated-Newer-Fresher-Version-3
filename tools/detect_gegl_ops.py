import os
import re
import sys

def get_used_ops(src_dir):
    ops = set()
    pattern = re.compile(r'["\']((?:gegl|unfv3):[a-z0-9-]+)["\']')
    for root, _, files in os.walk(src_dir):
        for file in files:
            if file.endswith(('.cpp', '.h')):
                try:
                    with open(os.path.join(root, file), 'r', errors='ignore') as f:
                        content = f.read()
                        matches = pattern.findall(content)
                        for m in matches:
                            ops.add(m)
                except Exception:
                    continue
    return ops

def find_op_modules(ops, plugins_dir):
    modules = set()
    if not os.path.exists(plugins_dir):
        return modules

    for entry in os.listdir(plugins_dir):
        if entry.endswith(('.so', '.dll', '.dylib')):
            path = os.path.join(plugins_dir, entry)
            try:
                # Portable way to read strings without the 'strings' utility
                with open(path, 'rb') as f:
                    content = f.read()
                    for op in ops:
                        # Convert op string to bytes for search
                        if op.encode('ascii') in content:
                            modules.add(path)
                            break
            except Exception:
                continue
    return modules

if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(1)
    src = sys.argv[1]
    plugins = sys.argv[2]

    used_ops = get_used_ops(src)
    matched_modules = find_op_modules(used_ops, plugins)

    # Core and common modules are always required
    for entry in os.listdir(plugins):
        if any(x in entry for x in ['gegl-common', 'gegl-core', 'gegl-generated']):
             matched_modules.add(os.path.join(plugins, entry))

    for m in sorted(list(matched_modules)):
        print(m.replace('\\', '/'))
