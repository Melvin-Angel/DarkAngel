"""Faithful ordered OOXML text extraction including full tables; no summaries."""
import pathlib, xml.etree.ElementTree as ET, zipfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
NS={'w':'http://schemas.openxmlformats.org/wordprocessingml/2006/main'}
def text(element):
    parts=[]
    for e in element.iter():
        if e.tag.endswith('}t'):parts.append(e.text or '')
        elif e.tag.endswith('}tab'):parts.append('\t')
        elif e.tag.endswith('}br'):parts.append('\n')
    return ''.join(parts)

def main():
    source=ROOT/'docs/architecture/DarkAngel_Engine_Architecture_Decision_Log_v0.1.docx'
    output=[]
    with zipfile.ZipFile(source) as archive:
        xml=ET.fromstring(archive.read('word/document.xml'));body=xml.find('w:body',NS)
        for element in body:
            if element.tag.endswith('}p'):output.append(text(element))
            elif element.tag.endswith('}tbl'):
                output.append('TABLE')
                for row in element.findall('w:tr',NS):
                    output.append(' | '.join(text(cell).replace('\n','\\n').replace('|','\\|') for cell in row.findall('w:tc',NS)))
                output.append('END TABLE')
        for part in ['word/footnotes.xml','word/endnotes.xml']:
            if part in archive.namelist():
                output.append(part)
                output.extend(text(p) for p in ET.fromstring(archive.read(part)).findall('.//w:p',NS))
        nodes=xml.findall('.//w:t',NS)
    dest=source.with_name('Decision_Log.txt');dest.write_text('\n\n'.join(output)+'\n',encoding='utf-8')
    print(f'Extracted {len(output)} ordered paragraphs/table rows; {len(nodes)} source text nodes; {dest.stat().st_size} bytes. Complete DOCX retained.')

if __name__=='__main__':main()
