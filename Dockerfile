# بيئة بناء مستقرة ومحدثة لتشغيل الكود
FROM ubuntu:22.04

# منع أي طلبات تداخل أثناء التثبيت
ENV DEBIAN_FRONTEND=noninteractive

# تحديث السيرفر وتثبيت أدوات المترجم ومكتبة PostgreSQL الدائمة
RUN apt-get update && apt-get install -y \
    g++ \
    make \
    libpq-dev \
    && rm -rf /var/lib/apt/lists/*

# تحديد مجلد العمل
WORKDIR /app

# نسخ الكود الجديد
COPY . .

# أمر بناء كود الـ C++ مع ربط مكتبة الـ PostgreSQL
RUN g++ -O3 main.cpp -o server -lpq -pthread

# فتح منفذ السيرفر
EXPOSE 8080

# تشغيل السيرفر
CMD ["./server"]
