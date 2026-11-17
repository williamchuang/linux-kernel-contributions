# usage: extract.py <file> <func-start-regex>... ; prints verbatim function definitions
import sys,re
src=open(sys.argv[1]).read().split('\n')
for pat in sys.argv[2:]:
    for i,l in enumerate(src):
        if re.match(pat,l):
            j=i
            while src[j]!='}': j+=1
            print('\n'.join(src[i:j+1])); print()
            break
    else: sys.exit('not found: '+pat)
