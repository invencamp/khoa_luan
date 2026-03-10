from PyPDF2 import PdfReader, PdfWriter
from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import letter
from reportlab.lib.colors import white, black
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.units import inch

# Đảo màu cơ bản (ý tưởng)
# Cách chính xác hơn là chuyển sang ảnh rồi đảo màu pixel.

from pdf2image import convert_from_path
from PIL import Image, ImageOps

pages = convert_from_path(r"C:\Users\admin\Documents\output_with_toc.pdf")
new_pages = []

for p in pages:
    inverted = ImageOps.invert(p.convert("RGB"))
    new_pages.append(inverted)

new_pages[0].save("output_convert.pdf", save_all=True, append_images=new_pages[1:])
