cd ~/git/xmrwallet/app

cat > fix_monerujo.py << 'EOF'
#!/usr/bin/env python3
import re

with open('src/main/cpp/monerujo.cpp', 'r') as f:
    content = f.read()

# 1. Add cassert include after wallet2_api.h
content = content.replace(
    '#include "wallet2_api.h"',
    '#include "wallet2_api.h"\n#include <cassert>'
)

# 2. Replace the coins() method call
content = content.replace(
    'return reinterpret_cast<jlong>(wallet->coins());',
    'return 0; // wallet->coins() removed in newer versions'
)

# 3. Comment out CoinsInfo related functions
# Comment out newCoinsInfo function
content = re.sub(
    r'(jobject newCoinsInfo\(JNIEnv \*env, Monero::CoinsInfo \*info\) \{.*?\n\})',
    r'// \1',
    content,
    flags=re.DOTALL
)

# Comment out coinsInfoArrayList function  
content = re.sub(
    r'(jobject coinsInfoArrayList\(JNIEnv \*env, const std::vector<Monero::CoinsInfo \*> &vector,.*?\n\})',
    r'// \1',
    content,
    flags=re.DOTALL
)

with open('src/main/cpp/monerujo.cpp', 'w') as f:
    f.write(content)

print("Fixed using Python script")
EOF

python3 fix_monerujo.py
