# استخدام بيئة بناء تحتوي على مترجم C++ وثابتة الاستقرار
FROM ubuntu:22.04

# تجنب طلب أي تداخل من المستخدم أثناء التثبيت
ENV DEBIAN_FRONTEND=noninteractive

# تحديث النظام وتثبيت أدوات المترجم ومكتبة PostgreSQL الهامة
RUN apt-get update && apt-get install -y \
    g++ \
    make \
    libpq-dev \
    && rm -rf /var/lib/apt/lists/*

# تحديد مجلد العمل بداخل السيرفر
WORKDIR /app

# نسخ ملفات المشروع بالكامل من جيت هاب للسيرفر
COPY . .

# أمر بناء الكود مع ربط مكتبة postgres (إضافة -lpq و -pthread لضمان عمل السيرفر)
RUN g++ -O3 main.cpp -o server -lpq -pthread

# فتح المنفذ الافتراضي
EXPOSE 8080

# أمر تشغيل السيرفر تلقائياً عند الإقلاع
CMD ["./server"]
