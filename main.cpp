#include "httplib.h" 
#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <sqlite3.h> // مكتبة قاعدة البيانات الدائمة

using namespace std;

// هيكل البيانات لقراءة المعاينات من قاعدة البيانات
struct Inspection {
    int id;
    string client_name;
    string m_type;
    int width;
    int depth;
    float floors;
    int pit;
    int overhead;
    string status;
};

// الرقم السري لمدير التركيبات (يمكنك تغييره هنا)
const string ADMIN_PASSWORD = "123"; 

class Elevator {
private:
    const float P_BRACKET = 150.0;  
    const float P_BOLT = 15.0;      
    const float P_ROPE = 80.0;
    const float P_FISH = 120.0;

public:
    string get_door_type(int sa) {
        string d = "";
        if (sa >= 105 && sa <= 120) d += "Semi Auto 70 / ";
        if (sa >= 121 && sa <= 135) d += "Semi Auto 80 / ";
        if (sa >= 128 && sa < 144)  d += "Auto 70 SI / ";
        if (sa >= 144 && sa <= 160) d += "Auto 80 SI / ";
        if (sa >= 158 && sa <= 175) d += "Auto 90 SI / ";
        if (sa >= 175 && sa <= 200) d += "Auto 100 SI / ";
        if (sa >= 157 && sa <= 168) d += "Auto 70 CO / ";
        if (sa > 168 && sa <= 190)  d += "Auto 80 CO / ";
        if (sa > 190 && sa <= 210)  d += "Auto 90 CO / ";
        if (d == "") return "No standard door";
        return d.substr(0, d.length() - 3);
    }

    int get_cabin_dbg(int w) { return w - 30; }
    int get_cwt_dbg(int v) {
        if (v >= 100 && v <= 110) return 72;
        if (v > 110 && v <= 120) return 82;
        if (v > 120 && v <= 125) return 92;
        if (v > 125 && v <= 210) return 102;
        return 0; 
    }
    int get_cabin_width(int cw) { return cw - 40; }
    int get_cabin_depth(int cd) { return cd - 60; }
    float get_shaft_height(float f, string t) { return (t == "MRL") ? (f * 4) + 1.5 : (f * 4); }

    int calc_brackets(float h) { return (h / 2.0) * 4; }
    int calc_bolts(int b) { return b * 4; }
    float calc_ropes(float h) { return ((h * 2) + 5) * 4; }

    float get_p_bracket() { return P_BRACKET; }
    float get_p_bolt() { return P_BOLT; }
    float get_p_rope() { return P_ROPE; }
    float get_p_fish() { return P_FISH; }
};

// دالة مساعدة لإنشاء الجدول في بداية تشغيل البرنامج
void init_database() {
    sqlite3* db;
    if (sqlite3_open("elevators.db", &db) == SQLITE_OK) {
        string query = "CREATE TABLE IF NOT EXISTS inspections ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "client_name TEXT, m_type TEXT, width INTEGER, depth INTEGER, "
                       "floors REAL, pit INTEGER, overhead INTEGER, status TEXT);";
        sqlite3_exec(db, query.c_str(), 0, 0, 0);
    }
    sqlite3_close(db);
}

int main() {
    init_database(); // تجهيز قاعدة البيانات تلقائياً
    httplib::Server svr;
    Elevator elevator;

    // 1. واجهة الفني لرفع المقاسات
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        string html = "<html><head><meta charset='UTF-8'><style>"
                      "body{background:#f0f2f5;font-family:sans-serif;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0;}"
                      ".card{background:white;padding:25px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);width:100%;max-width:400px;direction:rtl;text-align:right;box-sizing:border-box;}"
                      "h2{color:#28a745;text-align:center;margin-bottom:15px;}.f-group{margin-bottom:10px;}"
                      "label{font-weight:600;color:#495057;display:block;margin-bottom:4px;font-size:14px;}"
                      "input,select{width:100%;padding:8px;border:1px solid #ced4da;border-radius:6px;box-sizing:border-box;text-align:center;font-size:16px;background:#f8f9fa;}"
                      "button{background:#007bff;color:white;border:none;padding:12px;border-radius:6px;width:100%;font-size:16px;font-weight:bold;cursor:pointer;margin-top:10px;}"
                      "a{display:block;text-align:center;margin-top:15px;color:#6c757d;text-decoration:none;font-size:14px;}"
                      "</style></head><body><div class='card'><h2>👷‍♂️ لوحة الفني: رفع معاينة</h2>"
                      "<form action='/save' method='get'>"
                      "<div class='f-group'><label>اسم العميل:</label><input type='text' name='c_name' required placeholder='أدخل اسم العميل بالكامل'></div>"
                      "<div class='f-group'><label>نوع النظام:</label><select name='m_type'><option value='MR'>بغرفة محرك (MR)</option><option value='MRL'>بدون غرفة محرك (MRL)</option></select></div>"
                      "<div class='f-group'><label>1. عرض البئر (CM):</label><input type='number' name='width' required></div>"
                      "<div class='f-group'><label>2. عمق البئر (CM):</label><input type='number' name='depth' required></div>"
                      "<div class='f-group'><label>3. عدد الأدوار:</label><input type='number' name='floors' required></div>"
                      "<div class='f-group'><label>4. عمق الحفرة (CM):</label><input type='number' name='pit' required></div>"
                      "<div class='f-group'><label>5. الارتفاع العلوي (CM):</label><input type='number' name='overhead' required></div>"
                      "<button type='submit'>💾 حفظ المقاسات وإرسال للمراجعة</button></form>"
                      "<a href='/admin-login'>💼 دخول مدير التركيبات ←</a></div></body></html>";
        res.set_content(html, "text/html; charset=utf-8");
    });

    // 2. حفظ البيانات في ملف SQLite
    svr.Get("/save", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("c_name") || !req.has_param("width") || !req.has_param("depth")) {
            res.set_content("خطأ في البيانات", "text/plain; charset=utf-8");
            return;
        }

        string name = req.get_param_value("c_name");
        string m_type = req.get_param_value("m_type");
        int width = stoi(req.get_param_value("width"));
        int depth = stoi(req.get_param_value("depth"));
        float floors = stof(req.get_param_value("floors"));
        int pit = stoi(req.get_param_value("pit"));
        int overhead = stoi(req.get_param_value("overhead"));
        string status = "قيد المراجعة";

        sqlite3* db;
        if (sqlite3_open("elevators.db", &db) == SQLITE_OK) {
            string query = "INSERT INTO inspections (client_name, m_type, width, depth, floors, pit, overhead, status) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
            sqlite3_stmt* stmt;
            sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, 0);
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, m_type.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 3, width);
            sqlite3_bind_int(stmt, 4, depth);
            sqlite3_bind_double(stmt, 5, floors);
            sqlite3_bind_int(stmt, 6, pit);
            sqlite3_bind_int(stmt, 7, overhead);
            sqlite3_bind_text(stmt, 8, status.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);

        string success = "<html><head><meta charset='UTF-8'></head><body style='font-family:sans-serif; text-align:center; padding-top:50px; direction:rtl;'>"
                         "<h2 style='color:#28a745;'>✅ تم حفظ المعاينة بشكل دائم!</h2>"
                         "<p>تم إرسال معاينة العميل (<b>" + name + "</b>) إلى الإدارة.</p>"
                         "<br><a href='/' style='background:#007bff; color:white; padding:10px 20px; text-decoration:none; border-radius:5px;'>إضافة معاينة جديدة</a>"
                         "</body></html>";
        res.set_content(success, "text/html; charset=utf-8");
    });

    // 3. صفحة تسجيل دخول المدير
    svr.Get("/admin-login", [](const httplib::Request&, httplib::Response& res) {
        string html = "<html><head><meta charset='UTF-8'><style>"
                      "body{background:#f0f2f5;font-family:sans-serif;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0;}"
                      ".card{background:white;padding:25px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);width:100%;max-width:350px;direction:rtl;text-align:center;}"
                      "input{width:100%;padding:10px;margin:10px 0;border:1px solid #ced4da;border-radius:6px;box-sizing:border-box;font-size:16px;text-align:center;}"
                      "button{background:#343a40;color:white;border:none;padding:12px;border-radius:6px;width:100%;font-size:16px;cursor:pointer;}"
                      "</style></head><body><div class='card'><h2>💼 دخول المدير</h2>"
                      "<form action='/admin' method='get'>"
                      "<input type='password' name='password' placeholder='أدخل الرقم السري' required>"
                      "<button type='submit'>دخول اللوحة</button></form></div></body></html>";
        res.set_content(html, "text/html; charset=utf-8");
    });

    // 4. لوحة تحكم المدير (تتحقق من الرقم السري وتقرأ من قاعدة البيانات)
    svr.Get("/admin", [](const httplib::Request& req, httplib::Response& res) {
        string pass = req.get_param_value("password");
        if (pass != ADMIN_PASSWORD) {
            res.set_content("<html><head><meta charset='UTF-8'></head><body style='text-align:center;padding-top:50px;'><h2>❌ الرقم السري خاطئ!</h2><a href='/admin-login'>حاول مجدداً</a></body></html>", "text/html; charset=utf-8");
            return;
        }

        vector<Inspection> list;
        sqlite3* db;
        if (sqlite3_open("elevators.db", &db) == SQLITE_OK) {
            sqlite3_stmt* stmt;
            string query = "SELECT id, client_name, m_type, width, depth, floors, pit, overhead, status FROM inspections ORDER BY id DESC;";
            if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, 0) == SQLITE_OK) {
