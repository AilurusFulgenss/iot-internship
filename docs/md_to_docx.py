from docx import Document
from docx.shared import Pt, RGBColor, Inches
from docx.enum.text import WD_ALIGN_PARAGRAPH
import re, sys, os

def md_to_docx(md_path, docx_path):
    doc = Document()

    # Page margins
    for section in doc.sections:
        section.top_margin    = Inches(1)
        section.bottom_margin = Inches(1)
        section.left_margin   = Inches(1.2)
        section.right_margin  = Inches(1.2)

    with open(md_path, encoding="utf-8") as f:
        lines = f.readlines()

    for line in lines:
        line = line.rstrip("\n")

        if line.startswith("# ") and not line.startswith("## "):
            p = doc.add_heading(line[2:], level=1)
            p.runs[0].font.color.rgb = RGBColor(0x00, 0x70, 0xC0)

        elif line.startswith("## "):
            p = doc.add_heading(line[3:], level=2)
            p.runs[0].font.color.rgb = RGBColor(0x00, 0x46, 0x8C)

        elif re.match(r"^\d+\. ", line):
            p = doc.add_paragraph(style="List Number")
            p.add_run(line[line.index(". ") + 2:])
            p.paragraph_format.left_indent = Inches(0.25)

        elif line.startswith("- "):
            p = doc.add_paragraph(style="List Bullet")
            text = line[2:]
            # bold **text**
            parts = re.split(r"(\*\*.*?\*\*)", text)
            for part in parts:
                if part.startswith("**") and part.endswith("**"):
                    p.add_run(part[2:-2]).bold = True
                else:
                    p.add_run(part)
            p.paragraph_format.left_indent = Inches(0.25)

        elif line.startswith("---") or line.startswith("___"):
            doc.add_paragraph("─" * 60)

        elif line.strip() == "":
            doc.add_paragraph("")

        else:
            p = doc.add_paragraph()
            parts = re.split(r"(\*\*.*?\*\*|`.*?`)", line)
            for part in parts:
                if part.startswith("**") and part.endswith("**"):
                    p.add_run(part[2:-2]).bold = True
                elif part.startswith("`") and part.endswith("`"):
                    run = p.add_run(part[1:-1])
                    run.font.name = "Courier New"
                    run.font.size = Pt(10)
                else:
                    p.add_run(part)

    doc.save(docx_path)
    print(f"Saved: {docx_path}")

if __name__ == "__main__":
    base = os.path.dirname(os.path.abspath(__file__))
    md_to_docx(os.path.join(base, "week-01-report.md"),
               os.path.join(base, "week-01-report.docx"))
    md_to_docx(os.path.join(base, "week-02-report.md"),
               os.path.join(base, "week-02-report.docx"))
