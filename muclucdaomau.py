from PyPDF2 import PdfReader, PdfWriter
from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import A4
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from pdf2image import convert_from_path
from PIL import Image, ImageOps
import io
import re
import os

def setup_vietnamese_font():
    """Thiết lập font hỗ trợ tiếng Việt"""
    try:
        font_paths = [
            'C:\\Windows\\Fonts\\Arial.ttf',
            'C:\\Windows\\Fonts\\times.ttf',
            '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
            '/System/Library/Fonts/Supplemental/Arial Unicode.ttf',
        ]
        
        for font_path in font_paths:
            if os.path.exists(font_path):
                try:
                    pdfmetrics.registerFont(TTFont('VietnameseFont', font_path))
                    print(f"✓ Đã đăng ký font: {font_path}")
                    return 'VietnameseFont'
                except Exception as e:
                    continue
        
        print("⚠ Không tìm thấy font hỗ trợ tiếng Việt, sử dụng Helvetica")
        return None
    except Exception as e:
        print(f"Lỗi thiết lập font: {e}")
        return None

def extract_headings_from_pdf(pdf_path):
    """Tự động phát hiện các tiêu đề/mục trong PDF (bao gồm cả mục con)"""
    reader = PdfReader(pdf_path)
    toc_items = []
    
    # Cách 1: Lấy từ bookmarks/outlines nếu có
    if reader.outline:
        def process_outline(outline, level=0):
            for item in outline:
                if isinstance(item, list):
                    process_outline(item, level + 1)
                else:
                    try:
                        page_num = reader.get_destination_page_number(item) + 1
                        title = item.title
                        indent = "  " * level
                        toc_items.append((f"{indent}{title}", page_num))
                    except:
                        pass
        
        process_outline(reader.outline)
    
    # Cách 2: Nếu không có bookmarks, tìm text với pattern phân cấp
    if not toc_items:
        for page_num, page in enumerate(reader.pages, 1):
            text = page.extract_text()
            lines = text.split('\n')
            
            for line in lines:
                line = line.strip()
                
                # Patterns cho tiêu đề cấp 1 (chương chính)
                level1_patterns = [
                    (r'^(Chương|Chapter|CHƯƠNG)\s+[IVXLCDM\d]+', 0),
                    (r'^(Phần|Part|PHẦN)\s+[IVXLCDM\d]+', 0),
                    (r'^[IVXLCDM]+\.\s+[A-ZÀÁẠẢÃÂẦẤẬẨẪĂẰẮẶẲẴÈÉẸẺẼÊỀẾỆỂỄÌÍỊỈĨÒÓỌỎÕÔỒỐỘỔỖƠỜỚỢỞỠÙÚỤỦŨƯỪỨỰỬỮỲÝỴỶỸĐ]', 0),
                ]
                
                # Patterns cho tiêu đề cấp 2 (mục con)
                level2_patterns = [
                    (r'^[IVXLCDM]+\.\d+\s+', 1),  # I.1, I.2, II.1...
                    (r'^\d+\.\d+\s+', 1),  # 1.1, 1.2, 2.1...
                    (r'^[IVXLCDM]+\.\d+\.\s+', 1),  # I.1., I.2., ...
                ]
                
                # Patterns cho tiêu đề cấp 3 (mục con của mục con)
                level3_patterns = [
                    (r'^[IVXLCDM]+\.\d+\.\d+\s+', 2),  # I.1.1, I.1.2...
                    (r'^\d+\.\d+\.\d+\s+', 2),  # 1.1.1, 1.1.2...
                ]
                
                # Kiểm tra tất cả các patterns
                all_patterns = level1_patterns + level2_patterns + level3_patterns
                matched = False
                
                for pattern, level in all_patterns:
                    if re.match(pattern, line, re.IGNORECASE):
                        # Chỉ lấy tiêu đề có độ dài hợp lý
                        if 5 < len(line) < 150:
                            indent = "  " * level
                            toc_items.append((f"{indent}{line}", page_num))
                            matched = True
                            break
                
                # Nếu không match pattern nào nhưng line bắt đầu bằng số hoặc chữ La Mã
                # và chữ cái đầu tiên sau đó là chữ hoa
                if not matched and len(line) > 5:
                    simple_patterns = [
                        (r'^(\d+|[IVXLCDM]+)\.\s*[A-ZÀÁẠẢÃÂẦẤẬẨẪĂẰẮẶẲẴÈÉẸẺẼÊỀẾỆỂỄÌÍỊỈĨÒÓỌỎÕÔỒỐỘỔỖƠỜỚỢỞỠÙÚỤỦŨƯỪỨỰỬỮỲÝỴỶỸĐ]', 0),
                    ]
                    for pattern, level in simple_patterns:
                        if re.match(pattern, line):
                            if len(line) < 150:
                                indent = "  " * level
                                toc_items.append((f"{indent}{line}", page_num))
                                break
    
    # Loại bỏ trùng lặp và sắp xếp theo trang
    seen = set()
    unique_toc = []
    for item in toc_items:
        title, page = item
        key = (title.strip().lower(), page)
        if key not in seen:
            seen.add(key)
            unique_toc.append(item)
    
    return unique_toc

def create_toc_page(toc_items, start_page=1, font_name='VietnameseFont'):
    """Tạo trang mục lục với nền trắng, chữ đen và hỗ trợ phân cấp"""
    packet = io.BytesIO()
    can = canvas.Canvas(packet, pagesize=A4)
    width, height = A4
    
    if font_name is None:
        font_name = 'Helvetica'
    
    # Vẽ nền trắng
    can.setFillColorRGB(1, 1, 1)  # Màu trắng
    can.rect(0, 0, width, height, fill=1, stroke=0)
    
    # Tiêu đề mục lục - chữ đen
    can.setFillColorRGB(0, 0, 0)  # Màu đen
    can.setFont(font_name, 24)
    can.drawCentredString(width/2, height - 60, "MỤC LỤC")
    
    # Đường kẻ trang trí - màu đen
    can.setStrokeColorRGB(0, 0, 0)
    can.setLineWidth(2)
    can.line(50, height - 75, width - 50, height - 75)
    
    # Nội dung mục lục - chữ đen
    y_position = height - 110
    items_per_page = 35  # Tăng số mục trên mỗi trang
    
    for i, (title, page_num) in enumerate(toc_items):
        if y_position < 80:
            can.setFont(font_name, 9)
            can.drawCentredString(width/2, 40, f"Trang {start_page + (i // items_per_page)}")
            can.showPage()
            
            # Vẽ nền trắng cho trang mới
            can.setFillColorRGB(1, 1, 1)
            can.rect(0, 0, width, height, fill=1, stroke=0)
            can.setFillColorRGB(0, 0, 0)
            
            y_position = height - 50
        
        # Tính toán indent dựa trên số khoảng trắng đầu tiên
        indent_level = len(title) - len(title.lstrip())
        title_clean = title.strip()
        
        # Điều chỉnh kích thước font theo cấp độ
        if indent_level == 0:  # Cấp 1 - tiêu đề chính
            font_size = 11
            x_start = 60
        elif indent_level <= 2:  # Cấp 2 - mục con
            font_size = 10
            x_start = 80
        else:  # Cấp 3+ - mục con của mục con
            font_size = 9
            x_start = 100
        
        can.setFont(font_name, font_size)
        
        # Tính toán độ rộng tối đa cho tiêu đề
        max_width = width - x_start - 100
        
        # Cắt tiêu đề nếu quá dài
        if can.stringWidth(title_clean, font_name, font_size) > max_width:
            while can.stringWidth(title_clean + "...", font_name, font_size) > max_width and len(title_clean) > 0:
                title_clean = title_clean[:-1]
            title_clean += "..."
        
        # Vẽ tiêu đề
        can.drawString(x_start, y_position, title_clean)
        
        # Vẽ dấu chấm nối
        dots_start = x_start + can.stringWidth(title_clean, font_name, font_size) + 3
        dots_end = width - 75
        
        if dots_end > dots_start:
            dot_string = "." * int((dots_end - dots_start) / 3)
            can.setFont(font_name, 8)
            can.drawString(dots_start, y_position, dot_string)
            can.setFont(font_name, font_size)
        
        # Vẽ số trang
        adjusted_page = page_num + len(toc_items) // items_per_page + 1
        can.setFont(font_name, 10)
        can.drawRightString(width - 55, y_position, str(adjusted_page))
        
        # Điều chỉnh khoảng cách giữa các dòng
        if indent_level == 0:
            y_position -= 22
        else:
            y_position -= 18
    
    can.setFont(font_name, 9)
    can.drawCentredString(width/2, 40, f"Trang {start_page + len(toc_items) // items_per_page}")
    
    can.save()
    packet.seek(0)
    
    return PdfReader(packet)

def add_page_numbers(font_name='VietnameseFont', page_num=1, total_pages=1):
    """Tạo overlay số trang với nền trắng và chữ đen"""
    packet = io.BytesIO()
    can = canvas.Canvas(packet, pagesize=A4)
    width, height = A4
    
    if font_name is None:
        font_name = 'Helvetica'
    
    # Vẽ hình chữ nhật trắng làm nền cho số trang
    can.setFillColorRGB(1, 1, 1)  # Nền trắng
    can.rect(0, 0, width, 50, fill=1, stroke=0)
    
    # Vẽ số trang màu đen
    can.setFillColorRGB(0, 0, 0)  # Chữ đen
    can.setFont(font_name, 10)
    can.drawCentredString(width/2, 30, f"Trang {page_num} / {total_pages}")
    
    can.save()
    packet.seek(0)
    
    return PdfReader(packet).pages[0]

def convert_to_white_background(input_pdf, dpi=200):
    """Chuyển đổi PDF sang ảnh rồi đảm bảo nền trắng, chữ đen"""
    print(f"\nĐang chuyển đổi PDF sang nền trắng (DPI={dpi})...")
    
    pages = convert_from_path(input_pdf, dpi=dpi)
    new_pages = []
    
    print(f"Đang xử lý {len(pages)} trang...")
    for i, page in enumerate(pages, 1):
        print(f"  - Trang {i}/{len(pages)}")
        
        # Chuyển sang RGB nếu cần
        if page.mode != 'RGB':
            page = page.convert('RGB')
        
        # Đảo màu để có nền trắng, chữ đen
        inverted = ImageOps.invert(page)
        new_pages.append(inverted)
    
    return new_pages

def create_white_background_pdf(input_pdf, 
                               output_pdf="output_white_bg.pdf",
                               auto_detect=True,
                               manual_toc=None,
                               add_page_nums=True,
                               dpi=200):
    """
    Tạo PDF với nền trắng, chữ đen, có mục lục và số trang
    
    Args:
        input_pdf: File PDF đầu vào (bất kỳ màu nền nào)
        output_pdf: File PDF đầu ra (nền trắng, chữ đen)
        auto_detect: Tự động phát hiện mục
        manual_toc: Danh sách mục thủ công
        add_page_nums: Thêm số trang
        dpi: Độ phân giải (200-300 khuyến nghị)
    """
    print("=" * 60)
    print("TẠO PDF NỀN TRẮNG, CHỮ ĐEN")
    print("=" * 60)
    
    # Bước 1: Thiết lập font
    print("\nĐang thiết lập font tiếng Việt...")
    font_name = setup_vietnamese_font()
    
    # Bước 2: Trích xuất mục lục
    reader = PdfReader(input_pdf)
    original_page_count = len(reader.pages)
    
    if auto_detect:
        print("\nĐang tự động phát hiện các mục trong PDF...")
        toc_items = extract_headings_from_pdf(input_pdf)
        if not toc_items:
            print("⚠ Không tìm thấy mục tự động. Tiếp tục không có mục lục.")
            toc_items = []
    else:
        toc_items = manual_toc if manual_toc else []
    
    if toc_items:
        print(f"\nTìm thấy {len(toc_items)} mục:")
        for title, page in toc_items[:10]:
            print(f"  - {title}: trang {page}")
        if len(toc_items) > 10:
            print(f"  ... và {len(toc_items) - 10} mục khác")
    
    # Bước 3: Chuyển đổi PDF gốc sang ảnh với nền trắng
    converted_pages = convert_to_white_background(input_pdf, dpi=dpi)
    
    # Bước 4: Tạo trang mục lục (nếu có)
    toc_page_count = 0
    toc_images = []
    
    if toc_items:
        print("\nĐang tạo trang mục lục...")
        toc_pdf = create_toc_page(toc_items, start_page=1, font_name=font_name)
        toc_page_count = len(toc_pdf.pages)
        
        # Chuyển trang mục lục sang ảnh
        temp_toc_path = "temp_toc.pdf"
        temp_writer = PdfWriter()
        for page in toc_pdf.pages:
            temp_writer.add_page(page)
        with open(temp_toc_path, 'wb') as f:
            temp_writer.write(f)
        
        toc_images = convert_from_path(temp_toc_path, dpi=dpi)
        os.remove(temp_toc_path)
    
    # Bước 5: Kết hợp tất cả và thêm số trang
    all_pages = toc_images + converted_pages
    total_pages = len(all_pages)
    
    print(f"\nĐang thêm số trang và lưu file...")
    
    # Import thêm cho việc vẽ text
    from PIL import ImageDraw, ImageFont
    
    # Tạo PDF từ ảnh với số trang
    final_pages = []
    for i, img in enumerate(all_pages, 1):
        if add_page_nums:
            # Tạo bản sao để vẽ lên
            final_img = img.copy()
            draw = ImageDraw.Draw(final_img)
            
            # Vẽ hình chữ nhật trắng làm nền cho số trang
            padding = 60
            rect_height = 50
            draw.rectangle(
                [(0, 0), (final_img.width, rect_height)],
                fill=(255, 255, 255)
            )
            
            # Vẽ số trang
            text = f"Trang {i} / {total_pages}"
            
            # Thử load font, nếu không được thì dùng default
            try:
                # Thử load Arial hoặc font khác
                font_size = int(dpi / 10)  # Tương ứng với DPI
                try:
                    font = ImageFont.truetype("arial.ttf", font_size)
                except:
                    try:
                        font = ImageFont.truetype("C:\\Windows\\Fonts\\Arial.ttf", font_size)
                    except:
                        font = ImageFont.load_default()
            except:
                font = ImageFont.load_default()
            
            # Tính toán vị trí để căn giữa
            bbox = draw.textbbox((0, 0), text, font=font)
            text_width = bbox[2] - bbox[0]
            text_height = bbox[3] - bbox[1]
            
            x = (final_img.width - text_width) // 2
            y = (rect_height - text_height) // 2
            
            # Vẽ text màu đen
            draw.text((x, y), text, fill=(0, 0, 0), font=font)
            
            final_pages.append(final_img)
        else:
            final_pages.append(img)
    
    # Lưu PDF
    print(f"Đang lưu file: {output_pdf}")
    final_pages[0].save(output_pdf, save_all=True, append_images=final_pages[1:], resolution=dpi)
    
    # Thêm bookmarks nếu có mục lục
    if toc_items:
        print("Đang thêm bookmarks...")
        reader_final = PdfReader(output_pdf)
        writer_final = PdfWriter()
        
        for page in reader_final.pages:
            writer_final.add_page(page)
        
        for title, page_num in toc_items:
            adjusted_page = page_num + toc_page_count - 1
            if adjusted_page < total_pages:
                writer_final.add_outline_item(title.strip(), adjusted_page)
        
        with open(output_pdf, 'wb') as f:
            writer_final.write(f)
    
    print(f"\n✓ HOÀN THÀNH!")
    print(f"  - Tổng số trang: {total_pages}")
    print(f"  - Số trang mục lục: {toc_page_count}")
    print(f"  - Số trang gốc: {original_page_count}")
    print(f"  - File đầu ra: {output_pdf}")
    print("=" * 60)

# ===== SỬ DỤNG =====
if __name__ == "__main__":
    
    # CÁCH 1: Tự động - Tạo PDF nền trắng với mục lục và số trang
    print("=== TẠO PDF NỀN TRẮNG - TỰ ĐỘNG ===\n")
    
    create_white_background_pdf(
        input_pdf="Giao-trinh-thuy-khi-dong-luc-ung-dung-Vu-Duy-Quang (1).pdf",
        output_pdf="out.pdf",
        auto_detect=True,
        add_page_nums=True,
        dpi=200  # Tăng lên 300 nếu muốn chất lượng cao hơn
    )
    
    # CÁCH 2: Sử dụng mục lục thủ công
    # manual_toc = [
    #     ("Lời mở đầu", 1),
    #     ("Chương 1: Giới thiệu", 3),
    #     ("  1.1 Tổng quan", 5),
    #     ("  1.2 Mục tiêu", 8),
    #     ("Chương 2: Nội dung", 10),
    #     ("Kết luận", 25)
    # ]
    # 
    # create_white_background_pdf(
    #     input_pdf="ch1_modau.pdf",
    #     output_pdf="output_manual_white.pdf",
    #     auto_detect=False,
    #     manual_toc=manual_toc,
    #     add_page_nums=True,
    #     dpi=200
    # )
    
    # CÁCH 3: Không có mục lục, chỉ chuyển nền trắng và thêm số trang
    # create_white_background_pdf(
    #     input_pdf="ch1_modau.pdf",
    #     output_pdf="output_simple_white.pdf",
    #     auto_detect=False,
    #     manual_toc=None,
    #     add_page_nums=True,
    #     dpi=200
    # )