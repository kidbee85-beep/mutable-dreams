from pathlib import Path
import re,ast,json
R=Path(__file__).resolve().parents[1];s=(R/'app/app.c').read_text();h=(R/'app/build/gen/felucca_font.h').read_text();adv={x:list(map(int,re.search(r'FONT_'+x+r'_ADV\[\d+\] = \{([^}]+)',h)[1].split(','))) for x in ['H','B']}
chunks=[s[s.index('static const char *help_lines'):s.index('static int md_context_help')],s[s.index('  static const char *soundhelp'):s.index('  reader_help_title=title;')]]
strings=[]
for c in chunks:
 for m in re.finditer(r'"(?:[^"\\]|\\.)*"',c):
  t=ast.literal_eval(m[0]);width=sum(adv['H'][ord(k)-32] for k in t if 32<=ord(k)<=126);strings.append((width,t))
bad=[x for x in strings if x[0]>204];print(json.dumps({'help_lines_checked':len(strings),'min_font':'H (14px face,16px line height)','width_limit':204,'overflow':bad}));assert not bad

# Bound every complete five-line guide plus the longest title and separators to one ANN_MAX=240 frame.
source=(R/'app/app.c').read_text()
base=re.search(r'help_lines\[NB\]\[5\]=(\{[\s\S]*?\n\});',source)[1]
groups=ast.literal_eval(base.replace('{','[').replace('}',']'))
for match in re.finditer(r'static const char \*(?:const )?\w+\[\]=\{([^}]+)\}',chunks[1]):
 values=ast.literal_eval('['+match[1]+']')
 if len(values)==5:groups.append(values)
titles=[]
for name in ['help_title','dream_pages']:
 raw=re.search(r'static const char \*'+name+r'\[[^]]*\]=\{([^}]+)\}',source)[1]
 titles.extend(ast.literal_eval(x[0]) for x in re.finditer(r'"(?:[^"\\]|\\.)*"',raw))
titles+=['DREAM SETUP / HELP','STOP THE DREAM','KEEP THIS BIOME','MUTABLE DREAMS 2.4']
max_title=max(map(len,titles));max_frame=max(max_title+sum(map(len,g))+2*5 for g in groups)
assert max_frame<=240,('Reader truncation risk',max_frame)
assert all(sum(adv['H'][ord(k)-32] for k in t if 32<=ord(k)<=126)<=207 for t in titles),'Help title exceeds 207px'
print(json.dumps({'five_line_guides':len(groups),'max_reader_frame_upper_bound':max_frame,'ANN_MAX':240,'titles_checked':len(titles)}))
