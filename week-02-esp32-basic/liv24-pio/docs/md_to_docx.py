from docx import Document
from docx.shared import Pt, RGBColor, Inches
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml.ns import qn
from docx.oxml import OxmlElement
import re, sys, os

FONT_NAME = "Graphik"
FONT_FALLBACK = "Calibri"

def set_default_font(doc, font_name):
    style = doc.styles["Normal"]
    style.font.name = font_name
    style.element.rPr.rFonts.set(qn("w:eastAsia"), font_name)

def apply_font(run, font_name=FONT_NAME):
    run.font.name = font_name
    run._element.rPr.rFonts.set(qn("w:eastAsia"), font_name)

def add_table_row(table, cells, bold=False, bg_color=None):
    row = table.add_row()
    for i, text in enumerate(cells):
        cell = row.cells[i]
        cell.text = ""
        p = cell.paragraphs[0]
        run = p.add_run(text)
        run.font.name = FONT_NAME
        run.font.size = Pt(10)
        if bold:
            run.bold = True
        if bg_color:
            tc = cell._tc
            tcPr = tc.get_or_add_tcPr()
            shd = OxmlElement("w:shd")
            shd.set(qn("w:val"), "clear")
            shd.set(qn("w:color"), "auto")
            shd.set(qn("w:fill"), bg_color)
            tcPr.append(shd)
    return row

def parse_table(lines, start_idx):
    rows = []
    i = start_idx
    while i < len(lines):
        line = lines[i].rstrip("\n")
        if line.startswith("|"):
            cells = [c.strip() for c in line.strip("|").split("|")]
            rows.append(cells)
        elif line.strip() == "" or not line.startswith("|"):
            break
        i += 1
    return rows, i

def md_to_docx(md_path, docx_path):
    doc = Document()

    # Page margins
    for section in doc.sections:
        section.top_margin    = Inches(1)
        section.bottom_margin = Inches(1)
        section.left_margin   = Inches(1.2)
        section.right_margin  = Inches(1.2)

    # Default font
    set_default_font(doc, FONT_NAME)

    with open(md_path, encoding="utf-8") as f:
        lines = f.readlines()

    i = 0
    in_code_block = False
    code_lines = []

    while i < len(lines):
        line = lines[i].rstrip("\n")

        # Code block
        if line.startswith("```"):
            if not in_code_block:
                in_code_block = True
                code_lines = []
            else:
                in_code_block = False
                p = doc.add_paragraph()
                p.paragraph_format.left_indent = Inches(0.4)
                p.paragraph_format.space_before = Pt(4)
                p.paragraph_format.space_after  = Pt(4)
                run = p.add_run("\n".join(code_lines))
                run.font.name = "Courier New"
                run.font.size = Pt(9)
                run.font.color.rgb = RGBColor(0x1E, 0x1E, 0x1E)
                p.paragraph_format.space_after = Pt(6)
            i += 1
            continue

        if in_code_block:
            code_lines.append(line)
            i += 1
            continue

        # Table detection
        if line.startswith("|"):
            table_rows, i = parse_table(lines, i)
            # Filter out separator rows (---|---|)
            data_rows = [r for r in table_rows if not all(re.match(r"^-+$", c.strip("-").strip()) or c.strip() == "" for c in r)]
            if len(data_rows) < 1:
                continue
            col_count = len(data_rows[0])
            table = doc.add_table(rows=0, cols=col_count)
            table.style = "Table Grid"
            for row_idx, row_data in enumerate(data_rows):
                is_header = (row_idx == 0)
                add_table_row(table, row_data, bold=is_header,
                              bg_color="D6E4F0" if is_header else None)
            doc.add_paragraph("")
            continue

        # H1
        if line.startswith("# ") and not line.startswith("## "):
            p = doc.add_heading(line[2:], level=1)
            for run in p.runs:
                run.font.name = FONT_NAME
                run.font.color.rgb = RGBColor(0x00, 0x70, 0xC0)
                run.font.size = Pt(20)

        # H2
        elif line.startswith("## ") and not line.startswith("### "):
            p = doc.add_heading(line[3:], level=2)
            for run in p.runs:
                run.font.name = FONT_NAME
                run.font.color.rgb = RGBColor(0x00, 0x46, 0x8C)
                run.font.size = Pt(15)

        # H3
        elif line.startswith("### "):
            p = doc.add_heading(line[4:], level=3)
            for run in p.runs:
                run.font.name = FONT_NAME
                run.font.color.rgb = RGBColor(0x1F, 0x61, 0x97)
                run.font.size = Pt(12)

        # Numbered list
        elif re.match(r"^\d+\. ", line):
            p = doc.add_paragraph(style="List Number")
            p.paragraph_format.left_indent = Inches(0.3)
            _add_inline(p, line[line.index(". ") + 2:])

        # Bullet list
        elif line.startswith("- "):
            p = doc.add_paragraph(style="List Bullet")
            p.paragraph_format.left_indent = Inches(0.3)
            _add_inline(p, line[2:])

        # Blockquote / note
        elif line.startswith("> "):
            p = doc.add_paragraph()
            p.paragraph_format.left_indent  = Inches(0.4)
            p.paragraph_format.space_before = Pt(2)
            p.paragraph_format.space_after  = Pt(2)
            run = p.add_run(line[2:])
            run.font.name = FONT_NAME
            run.font.size = Pt(10)
            run.font.italic = True
            run.font.color.rgb = RGBColor(0x55, 0x55, 0x55)

        # Horizontal rule
        elif line.startswith("---") or line.startswith("___"):
            p = doc.add_paragraph()
            pPr = p._p.get_or_add_pPr()
            pBdr = OxmlElement("w:pBdr")
            bottom = OxmlElement("w:bottom")
            bottom.set(qn("w:val"), "single")
            bottom.set(qn("w:sz"), "6")
            bottom.set(qn("w:space"), "1")
            bottom.set(qn("w:color"), "AAAAAA")
            pBdr.append(bottom)
            pPr.append(pBdr)

        # Empty line
        elif line.strip() == "":
            doc.add_paragraph("")

        # Normal paragraph
        else:
            p = doc.add_paragraph()
            _add_inline(p, line)

        i += 1

    doc.save(docx_path)
    print(f"Saved: {docx_path}")

def _add_inline(p, text):
    parts = re.split(r"(\*\*.*?\*\*|`.*?`|\*.*?\*)", text)
    for part in parts:
        if part.startswith("**") and part.endswith("**"):
            run = p.add_run(part[2:-2])
            run.bold = True
            run.font.name = FONT_NAME
        elif part.startswith("`") and part.endswith("`"):
            run = p.add_run(part[1:-1])
            run.font.name = "Courier New"
            run.font.size = Pt(10)
            run.font.color.rgb = RGBColor(0xC7, 0x25, 0x4E)
        elif part.startswith("*") and part.endswith("*"):
            run = p.add_run(part[1:-1])
            run.italic = True
            run.font.name = FONT_NAME
        else:
            run = p.add_run(part)
            run.font.name = FONT_NAME

if __name__ == "__main__":
    base = os.path.dirname(os.path.abspath(__file__))

    files = [
        ("week-01-02-report.md",      "Intern-week-01-02-report.docx"),
        ("liv24-device-manual.md",    "liv24-device-manual.docx"),
        ("liv24-ha-manual.md",        "liv24-ha-manual.docx"),
    ]

    for md_name, docx_name in files:
        md_path   = os.path.join(base, md_name)
        docx_path = os.path.join(base, docx_name)
        if os.path.exists(md_path):
            md_to_docx(md_path, docx_path)
        else:
            print(f"Skipped (not found): {md_name}")
