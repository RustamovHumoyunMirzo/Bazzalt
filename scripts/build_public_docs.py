"""Refresh navigation/search around editable static HTML. Filenames are the registry."""
from __future__ import annotations
import argparse
import html
from html.parser import HTMLParser
import json
from pathlib import Path
import re

DEFAULT_ROOT = Path(__file__).resolve().parents[1] / 'docs/public'
VERSION = re.compile(r'^(\d+)\.(\d+)\.(\d+)(?:-([A-Za-z0-9.-]+))?$')
LANGUAGE = re.compile(r'^[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$')
ARTICLE = re.compile(r'<article\b[^>]*\bclass=["\']doc["\'][^>]*>([\s\S]*?)</article>')


class Content(HTMLParser):
    def __init__(self, source):
        super().__init__(convert_charrefs=True)
        self.ids=set();self.headings=[];self.text=[];self.paragraphs=[];self.title=''
        self._heading=None;self._paragraph=None
        self.feed(source)
        if not self.title: raise ValueError('Each page needs exactly one non-empty h1')

    def handle_starttag(self, tag, attrs):
        attrs=dict(attrs)
        if 'id' in attrs:
            if attrs['id'] in self.ids: raise ValueError(f'Duplicate anchor: {attrs["id"]}')
            self.ids.add(attrs['id'])
        if tag in ('h1','h2','h3'):
            if tag!='h1' and not attrs.get('id'): raise ValueError('Every h2/h3 needs a stable id')
            self._heading=dict(level=tag,id=attrs.get('id',''),text='')
        if tag=='p': self._paragraph=''

    def handle_data(self, text):
        self.text.append(text)
        if self._heading is not None: self._heading['text']+=text
        if self._paragraph is not None: self._paragraph+=text

    def handle_endtag(self, tag):
        if self._heading is not None and tag==self._heading['level']:
            self._heading['text']=self._heading['text'].strip()
            if tag=='h1':
                if self.title: raise ValueError('A page must have exactly one h1')
                self.title=self._heading['text']
            else: self.headings.append(self._heading)
            self._heading=None
        if tag=='p' and self._paragraph is not None:
            self.paragraphs.append(' '.join(self._paragraph.split()));self._paragraph=None


class LandingTexts(HTMLParser):
    def __init__(self, source):
        super().__init__();self.values={};self.key=None;self.depth=0;self.feed(source)

    def handle_starttag(self, tag, attrs):
        key=dict(attrs).get('data-i18n')
        if key: self.key=key;self.values[key]='';self.depth=1
        elif self.key: self.depth+=1

    def handle_data(self, data):
        if self.key: self.values[self.key]+=data

    def handle_endtag(self, tag):
        if self.key:
            self.depth-=1
            if not self.depth: self.key=None


def read_article(source):
    match=ARTICLE.search(source)
    if not match: raise ValueError('A guide needs <article class="doc"> ... </article>')
    return match[1].strip()


def discover(root):
    """No version list, language list, page list or API snapshot is needed."""
    options=json.loads((root/'site.json').read_text(encoding='utf-8')) if (root/'site.json').exists() else {}
    folders=[p for p in root.iterdir() if p.is_dir() and VERSION.fullmatch(p.name)]
    folders.sort(key=lambda p: tuple(map(int,VERSION.fullmatch(p.name).groups()[:3]))+(VERSION.fullmatch(p.name)[4] is None,VERSION.fullmatch(p.name)[4] or ''), reverse=True)
    records=[]
    for folder in folders:
        # pathlib ordering is case-insensitive on Windows, case-sensitive on
        # Linux. Never let the build host change the generated catalog.
        for path in sorted(folder.rglob('*.html'), key=lambda p: (p.relative_to(root).as_posix().casefold(), p.relative_to(root).as_posix())):
            if not LANGUAGE.fullmatch(path.stem): raise ValueError(f'Guide filename must be a language code: {path}')
            if not path.resolve().is_relative_to(root.resolve()): raise ValueError('A guide cannot escape the docs folder')
            source=path.read_text(encoding='utf-8');body=read_article(source);parsed=Content(body)
            identifier=path.parent.relative_to(folder).as_posix()
            if identifier=='.': raise ValueError('Place guides inside a subject folder, e.g. manual/en.html')
            records.append(dict(version=folder.name,language=path.stem,id=identifier,title=parsed.title,
                description=(parsed.paragraphs or [parsed.title])[0],text=' '.join(' '.join(parsed.text).split()),
                headings=parsed.headings,anchors=sorted(parsed.ids),url=path.relative_to(root).as_posix(),body=body))
    if not records: raise ValueError('No versioned HTML guides found')
    versions=[p.name for p in folders if any(r['version']==p.name for r in records)]
    languages=sorted({p['language'] for p in records},key=lambda l:(l!='en',l))
    return options,versions,languages,records


def tree(records, options, folder=''):
    """Folder nesting is navigation nesting, with no fixed depth."""
    own=next((p for p in records if p['id']==folder),None)
    names={p['id'][len(folder)+1 if folder else 0:].split('/')[0] for p in records if p['id'].startswith(folder+'/' if folder else '') and p['id']!=folder}
    order=options.get('navigation',{}).get(folder,{}).get('order',[]) if folder else options.get('sections',[])
    children=[]
    for name in sorted(names,key=lambda n:(order.index(n) if n in order else len(order),n.casefold(),n)):
        identifier=folder+'/'+name if folder else name
        children.append(tree(records,options,identifier))
    config=options.get('navigation',{}).get(folder,{})
    title=config.get('title',own['title'] if own else folder.rsplit('/',1)[-1].replace('-',' ').replace('_',' ').title())
    return dict(title=title,**({'id':folder} if own else {}),children=children)


def flatten_pages(nodes, trail=()):
    for node in nodes:
        if 'id' in node: yield dict(node,group=' / '.join(trail))
        yield from flatten_pages(node.get('children',[]),trail+(node['title'],))


def render_tree(nodes, available, prefix, current):
    by_id={p['id']:p for p in available}
    def link(p):
        active=' aria-current="page"' if p['id']==current else ''
        return f'<a class="nav-link" href="{prefix}{p["url"]}"{active}>{html.escape(p["title"])}</a>'
    def render(branch):
        parts=[]
        for node in branch:
            page=by_id.get(node.get('id'));children=render(node.get('children',[]))
            if not page and not children: continue
            if children:
                descendants={p['id'] for p in flatten_pages(node.get('children',[]))}
                opened=' open' if current in descendants or (page and page['id']==current) else ''
                parts.append(f'<details{opened}><summary>{html.escape(node["title"])}</summary><div class="tree-list">')
                if page: parts.append(link(page))
                parts.append(children+'</div></details>')
            elif page: parts.append(link(page))
        return '\n'.join(parts)
    return render(nodes)


def outputs(root):
    options,versions,languages,records=discover(root)
    template=(root/'templateExample.html').read_text(encoding='utf-8')
    result={}
    labels={l:'English' if l=='en' else l for l in languages} # Browser Intl supplies native language names.
    navigation={}
    for version in versions:
        for language in languages:
            available=[p for p in records if p['version']==version and p['language']==language]
            if not available: continue
            nodes=tree(available,options)['children']
            navigation.setdefault(version,{})[language]=nodes
            flattened=list(flatten_pages(nodes));by_id={p['id']:p for p in available}
            ordered=[by_id[p['id']] for p in flattened]
            for position,record in enumerate(ordered):
                prefix='../'*(len(record['id'].split('/'))+1)
                pager=[]
                for offset,label in ((-1,'Previous'),(1,'Next')):
                    if 0<=position+offset<len(ordered):
                        p=ordered[position+offset]
                        pager.append(f'<a class="{label.lower()}" href="{prefix}{p["url"]}"><small>{label}</small>{html.escape(p["title"])}</a>')
                version_options=''.join(f'<option value="{v}"{" selected" if v==version else ""}>{v}</option>' for v in versions)
                page_languages=[l for l in languages if any(p['version']==version and p['id']==record['id'] and p['language']==l for p in records)]
                language_options=''.join(f'<option value="{l}"{" selected" if l==language else ""}>{labels[l]}</option>' for l in page_languages)
                toc=''.join(f'<a class="toc-link{" sub" if h["level"]=="h3" else ""}" href="#{html.escape(h["id"],quote=True)}">{html.escape(h["text"])}</a>' for h in record['headings'])
                variables=dict(LANG=language,ROOT=prefix,TITLE=html.escape(record['title'],quote=True),DESCRIPTION=html.escape(record['description'],quote=True),
                    VERSION=version,PAGE=record['id'],LANGUAGE_NAME=labels[language],GROUP=html.escape(flattened[position]['group']),BODY=record['body'],
                    NAV='<noscript>Enable JavaScript for the navigation tree. Page content and links remain available.</noscript>',PAGER='\n'.join(pager),TOC=toc,VERSIONS=version_options,LANGUAGES=language_options,
                    BREADCRUMB=' / '.join(html.escape(part) for part in (flattened[position]['group'],record['title']) if part))
                result[root/record['url']]=re.sub(r'\{\{([A-Z_]+)\}\}',lambda m:variables[m[1]],template)
    index=(root/'index.html').read_text(encoding='utf-8')
    # Update real static links as well as the JS index; offline/no-JS readers also get the newest version.
    default=versions[0];default_language='en' if any(p['version']==default and p['language']=='en' for p in records) else next(p['language'] for p in records if p['version']==default)
    def landing_link(match):
        attrs=match[1];identifier=match[2]
        page=next((p for p in records if p['version']==default and p['language']==default_language and p['id']==identifier),None)
        return '<a'+re.sub(r'href="[^"]*"','href="'+page['url']+'"',attrs)+'data-doc-link="'+identifier+'"' if page else match[0]
    index=re.sub(r'<a([^>]*?)data-doc-link="([^"]+)"',landing_link,index)
    choices=''.join(f'<option value="{l}">{labels[l]}</option>' for l in languages)
    index=re.sub(r'(<select\b[^>]*id="language"[^>]*>)[\s\S]*?(</select>)',lambda m:m[1]+choices+m[2],index)
    result[root/'index.html']=index
    english=LandingTexts(index).values;landing={}
    for language in languages:
        path=root/'assets/languages'/f'{language}.json'
        translated=json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
        if set(translated)-set(english): raise ValueError(f'Unknown landing translation keys: {path}')
        landing[language]=dict(english,**translated)
    public=dict(defaultVersion=default,defaultLanguage=default_language,languages=labels,landing=landing,navigation=navigation,pages=[{k:v for k,v in p.items() if k!='body'} for p in records])
    result[root/'assets/catalog.js']='// Generated navigation/search data. Edit HTML pages, not this file.\nwindow.BazzaltDocs = '+json.dumps(public,ensure_ascii=False,separators=(',',':')).replace('<','\\u003c')+';\n'
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    generated=outputs(DEFAULT_ROOT);stale=[]
    for path,content in generated.items():
        if not path.exists() or path.read_text(encoding='utf-8')!=content:
            stale.append(path)
            if not args.check:
                path.parent.mkdir(parents=True,exist_ok=True);path.write_text(content,encoding='utf-8',newline='\n')
    if args.check and stale:
        for path in stale: print(f'Refresh needed: {path.relative_to(DEFAULT_ROOT)}')
        return 1
    print(f'Docs {"checked" if args.check else "refreshed"}: {len(generated)-2} editable pages.');return 0


if __name__=='__main__': raise SystemExit(main())
