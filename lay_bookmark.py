from PyPDF2 import PdfReader, PdfWriter
from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import A4
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
import io
import os

def setup_vietnamese_font():
    """Thiết lập font hỗ trợ tiếng Việt"""
    font_paths = [
        'C:\\Windows\\Fonts\\Arial.ttf',
        'C:\\Windows\\Fonts\\times.ttf',
        '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
        '/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf',
        '/System/Library/Fonts/Supplemental/Arial Unicode.ttf',
        '/Library/Fonts/Arial.ttf',
    ]
    
    for font_path in font_paths:
        if os.path.exists(font_path):
            try:
                pdfmetrics.registerFont(TTFont('VietnameseFont', font_path))
                print(f"✓ Đã đăng ký font: {font_path}")
                return 'VietnameseFont'
            except Exception as e:
                continue
    
    print("⚠ Không tìm thấy font tiếng Việt, sử dụng Helvetica")
    return 'Helvetica'

def extract_bookmarks(pdf_path):
    """Lấy tất cả bookmark từ PDF"""
    reader = PdfReader(pdf_path)
    bookmarks = []
    
    if not reader.outline:
        print("✗ PDF không có bookmark!")
        return None
    
    def process_outline(outline, level=0):
        for item in outline:
            if isinstance(item, list):
                process_outline(item, level + 1)
            else:
                try:
                    page_num = reader.get_destination_page_number(item) + 1
                    title = item.title
                    
                    # Xử lý encoding
                    if isinstance(title, bytes):
                        for encoding in ['utf-8', 'utf-16', 'latin-1', 'cp1252']:
                            try:
                                title = title.decode(encoding)
                                break
                            except:
                                continue
                    
                    bookmarks.append((title, page_num, level))
                    print(f"  {'  ' * level}• {title} → trang {page_num}")
                except Exception as e:
                    print(f"  Lỗi xử lý bookmark: {e}")
    
    print("\n✓ Bookmarks tìm thấy:")
    process_outline(reader.outline)
    
    return bookmarks

def create_toc_page(bookmarks, font_name='VietnameseFont'):
    """Tạo trang mục lục từ danh sách bookmark"""
    packet = io.BytesIO()
    can = canvas.Canvas(packet, pagesize=A4)
    width, height = A4
    
    items_per_page = 30
    num_toc_pages = (len(bookmarks) + items_per_page - 1) // items_per_page
    
    current_page = 1
    item_index = 0
    
    while item_index < len(bookmarks):
        # Tiêu đề
        can.setFont(font_name, 24)
        can.drawCentredString(width/2, height - 60, "MỤC LỤC")
        
        can.setLineWidth(2)
        can.line(50, height - 75, width - 50, height - 75)
        
        # Nội dung
        y_position = height - 110
        can.setFont(font_name, 11)
        
        page_items = 0
        while item_index < len(bookmarks) and page_items < items_per_page:
            title, page_num, level = bookmarks[item_index]
            
            # Indent theo level
            x_start = 70 + level * 15
            
            # Cắt tiêu đề nếu quá dài
            title_clean = title.strip()
            max_width = width - x_start - 100
            
            if can.stringWidth(title_clean, font_name, 11) > max_width:
                while can.stringWidth(title_clean + "...", font_name, 11) > max_width and len(title_clean) > 5:
                    title_clean = title_clean[:-1]
                title_clean += "..."
            
            try:
                can.drawString(x_start, y_position, title_clean)
            except:
                safe_title = title_clean.encode('ascii', 'ignore').decode('ascii')
                can.drawString(x_start, y_position, safe_title or "[...]")
            
            # Dấu chấm nối
            dots_start = x_start + can.stringWidth(title_clean, font_name, 11) + 5
            dots_end = width - 80
            num_dots = int((dots_end - dots_start) / 3)
            if num_dots > 0:
                can.drawString(dots_start, y_position, "." * num_dots)
            
            # Số trang (điều chỉnh thêm số trang mục lục)
            adjusted_page = page_num + num_toc_pages
            can.drawRightString(width - 60, y_position, str(adjusted_page))
            
            y_position -= 20
            item_index += 1
            page_items += 1
        
        # Footer
        can.setFont(font_name, 9)
        can.drawCentredString(width/2, 40, f"Trang {current_page}")
        
        if item_index < len(bookmarks):
            can.showPage()
            current_page += 1
    
    can.save()
    packet.seek(0)
    
    return PdfReader(packet)

def create_pdf_with_toc(input_pdf, output_pdf):
    """
    Tạo PDF với mục lục từ bookmark có sẵn
    
    Args:
        input_pdf: File PDF đầu vào (phải có bookmark)
        output_pdf: File PDF đầu ra
    """
    print("=" * 70)
    print("TẠO MỤC LỤC TỪ BOOKMARK CÓ SẴN")
    print("=" * 70)
    
    # Thiết lập font
    print("\n[1/5] Đang thiết lập font...")
    font_name = setup_vietnamese_font()
    
    # Đọc PDF
    print("\n[2/5] Đang đọc PDF...")
    try:
        reader = PdfReader(input_pdf)
        original_page_count = len(reader.pages)
        print(f"✓ Đọc thành công {original_page_count} trang")
    except Exception as e:
        print(f"✗ Lỗi đọc file: {e}")
        return
    
    # Lấy bookmark
    print("\n[3/5] Đang lấy bookmark...")
    bookmarks = extract_bookmarks(input_pdf)
    
    if not bookmarks:
        print("✗ Không thể tạo mục lục do PDF không có bookmark")
        return
    
    print(f"\n✓ Tìm thấy {len(bookmarks)} bookmark")
    
    # Tạo trang mục lục
    print("\n[4/5] Đang tạo trang mục lục...")
    toc_pdf = create_toc_page(bookmarks, font_name)
    toc_page_count = len(toc_pdf.pages)
    print(f"✓ Đã tạo {toc_page_count} trang mục lục")
    
    total_pages = toc_page_count + original_page_count
    
    # Ghép PDF
    print("\n[5/5] Đang ghép PDF...")
    writer = PdfWriter()
    
    # Thêm trang mục lục
    for page in toc_pdf.pages:
        writer.add_page(page)
    
    # Thêm trang gốc
    for page in reader.pages:
        writer.add_page(page)
    
    # Thêm bookmark mới
    for title, page_num, level in bookmarks:
        adjusted_page = page_num + toc_page_count - 1
        if adjusted_page < total_pages:
            try:
                writer.add_outline_item(title.strip(), adjusted_page)
            except:
                pass
    
    # Lưu file
    print(f"\nĐang lưu file: {output_pdf}")
    try:
        with open(output_pdf, 'wb') as f:
            writer.write(f)
    except Exception as e:
        print(f"✗ Lỗi lưu file: {e}")
        return
    
    print("\n" + "=" * 70)
    print("✓ HOÀN THÀNH!")
    print("=" * 70)
    print(f"Tổng số trang:     {total_pages}")
    print(f"Trang mục lục:     {toc_page_count}")
    print(f"Trang gốc:         {original_page_count}")
    print(f"Số bookmark:       {len(bookmarks)}")
    print(f"File đầu ra:       {output_pdf}")
    print("=" * 70)

# Sử dụng
if __name__ == "__main__":
    input_file = "Astrom.pdf"
    output_file = "output_with_.pdf"
    
    create_pdf_with_toc(
        input_file,
        output_file
    )