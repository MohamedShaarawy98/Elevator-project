#include "httplib.h"
#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <random>
#include <thread>
#include <postgresql/libpq-fe.h>

using namespace std;
using namespace std::chrono;

/* =========================================================================
   ملخص التعديلات الأمنية المضافة على النسخة الأصلية:

   1) إلغاء الاعتماد على باراميتر "role" القادم من المستخدم لتحديد صلاحيات
      المدير. الصلاحية الآن تُحدَّد فقط من جلسة (session) محمية بكوكي
      HttpOnly تم إنشاؤها بعد إدخال كلمة السر الصحيحة.
   2) حماية /delete بالكامل: لا يمكن الحذف إلا لمن معه جلسة مدير صالحة.
   3) كلمة سر المدير تُقرأ من متغير البيئة ADMIN_PASSWORD (مع قيمة احتياطية
      للتطوير فقط + تحذير في اللوج)، ومقارنتها تتم بزمن ثابت (secure_compare)
      لتقليل تسريب المعلومات عبر هجمات التوقيت، مع تأخير بسيط لإبطاء أي
      محاولات تخمين متكررة.
   4) تشفير/تهريب (escape) أي نص قادم من المستخدم قبل عرضه في HTML
      (اسم العميل) لمنع هجمات XSS.
   5) فحص وتحقق من كل المدخلات (الأرقام، نوع الموتور، الـ id) قبل استخدامها
      أو تخزينها، مع رسائل خطأ واضحة بدل تعطل السيرفر (crash).
   6) فحص نتيجة كل استعلام لقاعدة البيانات (PQresultStatus) بدل افتراض
      النجاح دائماً.
   7) تهيئة كل حقول Inspection بقيم افتراضية، وعرض رسالة "غير موجود" بدل
      حسابات على بيانات غير مهيأة (undefined behavior) عند عدم وجود السجل.
   8) إضافة صفحة تسجيل خروج (/admin-logout) تُلغي الجلسة وتمسح الكوكي.

   ملاحظات لتشغيل الإنتاج:
   - لازم تضبط متغير بيئة ADMIN_PASSWORD بكلمة سر قوية قبل التشغيل.
   - لو السيرفر شغال خلف HTTPS (مثلاً عبر nginx كـ reverse proxy)، فعّل
     متغير البيئة USE_HTTPS=1 عشان يضاف خاصية Secure على الكوكي.
   ========================================================================= */

struct Inspection {
    int id = 0;
    string client_name = "";
    string m_type = "";
    int width = 0;
    int depth = 0;
    float floors = 0;
    int pit = 0;
    int overhead = 0;
    string status = "";
};

// ---------------------------------------------------------------------------
// إعدادات وأدوات الأمان
// ---------------------------------------------------------------------------

string get_admin_password() {
    const char* env_pass = getenv("ADMIN_PASSWORD");
    if (env_pass && string(env_pass).length() > 0) {
        return string(env_pass);
    }
    cerr << "[تحذير أمني] متغير البيئة ADMIN_PASSWORD غير مضبوط، "
            "يتم استخدام كلمة سر افتراضية غير آمنة. لا تشغّل هذا في الإنتاج بدون ضبطها!" << endl;
    return "135790";
}

const string ADMIN_PASSWORD = get_admin_password();
const int SESSION_DURATION_MINUTES = 30;

unordered_map<string, steady_clock::time_point> active_sessions;
mutex sessions_mutex;

// مقارنة نصوص بزمن ثابت لتقليل تسريب معلومات التوقيت عند فحص كلمة السر
bool secure_compare(const string& a, const string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); i++) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

string generate_session_token() {
    random_device rd;
    static const char hex_chars[] = "0123456789abcdef";
    string token;
    token.reserve(64);
    for (int i = 0; i < 64; i++) {
        token += hex_chars[rd() % 16];
    }
    return token;
}

string create_session() {
    string token = generate_session_token();
    lock_guard<mutex> lock(sessions_mutex);
    active_sessions[token] = steady_clock::now() + minutes(SESSION_DURATION_MINUTES);
    return token;
}

void destroy_session(const string& token) {
    lock_guard<mutex> lock(sessions_mutex);
    active_sessions.erase(token);
}

string get_cookie_value(const httplib::Request& req, const string& name) {
    if (!req.has_header("Cookie")) return "";
    string cookie_header = req.get_header_value("Cookie");
    string target = name + "=";
    size_t pos = cookie_header.find(target);
    if (pos == string::npos) return "";
    pos += target.length();
    size_t end = cookie_header.find(';', pos);
    return cookie_header.substr(pos, end == string::npos ? string::npos : end - pos);
}

// الصلاحية الحقيقية الوحيدة المعتمدة: جلسة صالحة في الذاكرة، لا شيء غيرها.
bool is_admin_authenticated(const httplib::Request& req) {
    string token = get_cookie_value(req, "session_token");
    if (token.empty()) return false;

    lock_guard<mutex> lock(sessions_mutex);
    auto it = active_sessions.find(token);
    if (it == active_sessions.end()) return false;

    if (steady_clock::now() > it->second) {
        active_sessions.erase(it);
        return false;
    }
    it->second = steady_clock::now() + minutes(SESSION_DURATION_MINUTES); // sliding expiration
    return true;
}

string build_session_cookie(const string& token) {
    string cookie = "session_token=" + token + "; HttpOnly; Path=/; SameSite=Strict; Max-Age=" +
                     to_string(SESSION_DURATION_MINUTES * 60);
    if (getenv("USE_HTTPS")) cookie += "; Secure";
    return cookie;
}

string build_logout_cookie() {
    string cookie = "session_token=; HttpOnly; Path=/; SameSite=Strict; Max-Age=0";
    if (getenv("USE_HTTPS")) cookie += "; Secure";
    return cookie;
}

// ---------------------------------------------------------------------------
// حماية XSS + فحص المدخلات
// ---------------------------------------------------------------------------

string html_escape(const string& input) {
    string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

bool parse_int_safe(const string& s, int& out, int min_val, int max_val) {
    if (s.empty()) return false;
    try {
        size_t idx;
        int val = stoi(s, &idx);
        if (idx != s.size()) return false;
        if (val < min_val || val > max_val) return false;
        out = val;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_float_safe(const string& s, float& out, float min_val, float max_val) {
    if (s.empty()) return false;
    try {
        size_t idx;
        float val = stof(s, &idx);
        if (idx != s.size()) return false;
        if (val < min_val || val > max_val) return false;
        out = val;
        return true;
    } catch (...) {
        return false;
    }
}

string error_page(const string& message) {
    return "<html><head><meta charset='UTF-8'></head><body style='font-family:sans-serif;text-align:center;"
           "padding-top:50px;direction:rtl;'>"
           "<h2 style='color:#dc3545;'>⚠️ " + html_escape(message) + "</h2>"
           "<a href='/' style='color:#007bff;'>الرجوع للرئيسية</a></body></html>";
}

// ---------------------------------------------------------------------------
// منطق المصعد (بدون تغيير، غير مرتبط بالأمان)
// ---------------------------------------------------------------------------

class Elevator {
private:
    const float P_BRACKET = 150.0;
    const float P_BOLT = 25.0;
    const float P_ROPE = 80.0;
    const float P_FISH = 45.0;

public:
    string get_door_type(int sa) {
        if (sa >= 191 && sa <= 210) return "Auto 90 CO";
        else if (sa > 168 && sa <= 190)  return "Auto 80 CO";
        else if (sa >= 157 && sa <= 168) return "Auto 70 CO";
        else if (sa >= 175 && sa <= 200) return "Auto 100 SI";
        else if (sa >= 158 && sa <= 175) return "Auto 90 SI";
        else if (sa >= 144 && sa <= 160) return "Auto 80 SI";
        else if (sa >= 128 && sa < 144)  return "Auto 70 SI";
        else if (sa >= 121 && sa <= 135) return "Semi Auto 80";
        else if (sa >= 105 && sa <= 120) return "Semi Auto 70";
        return "No standard door";
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

// ---------------------------------------------------------------------------
// قاعدة البيانات
// ---------------------------------------------------------------------------

PGconn* connect_db() {
    const char* db_url = getenv("DATABASE_URL");
    if (!db_url) {
        db_url = "postgresql://postgres:password@localhost:5432/elevator_db";
    }
    PGconn* conn = PQconnectdb(db_url);
    if (PQstatus(conn) != CONNECTION_OK) {
        cerr << "Database Connection Failed: " << PQerrorMessage(conn) << endl;
    }
    return conn;
}

void init_database() {
    PGconn* conn = connect_db();
    if (PQstatus(conn) == CONNECTION_OK) {
        string query = "CREATE TABLE IF NOT EXISTS inspections ("
                       "id SERIAL PRIMARY KEY, "
                       "client_name TEXT, m_type TEXT, width INTEGER, depth INTEGER, "
                       "floors REAL, pit INTEGER, overhead INTEGER, status TEXT);";
        PGresult* res = PQexec(conn, query.c_str());
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            cerr << "فشل إنشاء الجدول: " << PQresultErrorMessage(res) << endl;
        }
        PQclear(res);
    }
    PQfinish(conn);
}

// يبني صفحة لوحة تحكم المدير (مستخدمة في GET و POST لـ /admin بدل تكرار الكود)
string render_admin_dashboard() {
    vector<Inspection> list;
    PGconn* conn = connect_db();
    if (PQstatus(conn) == CONNECTION_OK) {
        string query = "SELECT id, client_name, m_type, width, depth, floors, status FROM inspections ORDER BY id DESC;";
        PGresult* query_res = PQexec(conn, query.c_str());
        if (PQresultStatus(query_res) == PGRES_TUPLES_OK) {
            int rows = PQntuples(query_res);
            for (int i = 0; i < rows; i++) {
                Inspection insp;
                insp.id = stoi(PQgetvalue(query_res, i, 0));
                insp.client_name = PQgetvalue(query_res, i, 1);
                insp.m_type = PQgetvalue(query_res, i, 2);
                insp.width = stoi(PQgetvalue(query_res, i, 3));
                insp.depth = stoi(PQgetvalue(query_res, i, 4));
                insp.floors = stof(PQgetvalue(query_res, i, 5));
                insp.status = PQgetvalue(query_res, i, 6);
                list.push_back(insp);
            }
        } else {
            cerr << "خطأ في جلب البيانات: " << PQresultErrorMessage(query_res) << endl;
        }
        PQclear(query_res);
    }
    PQfinish(conn);

    ostringstream os;
    os << "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><style>"
       << "body{background:#f0f2f5;font-family:sans-serif;padding:20px;direction:rtl;text-align:right;}"
       << ".box{max-width:850px;margin:auto;background:white;padding:20px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);}"
       << "h2{color:#007bff;text-align:center;}table{width:100%;border-collapse:collapse;margin-top:20px;text-align:center;}"
       << "th,td{padding:12px;border-bottom:1px solid #dee2e6;}th{background:#343a40;color:white;}"
       << ".btn{background:#28a745;color:white;text-decoration:none;padding:6px 12px;border-radius:4px;font-size:14px;margin-left:5px;display:inline-block;}"
       << ".btn-del{background:#dc3545;color:white;text-decoration:none;padding:6px 12px;border-radius:4px;font-size:14px;display:inline-block;}"
       << ".table-container{width:100%; overflow-x:auto;}"
       << ".logout{display:block;text-align:left;margin-bottom:10px;}"
       << "</style></head><body><div class='box'>"
       << "<div class='logout'><a href='/admin-logout' style='color:#dc3545;font-weight:bold;text-decoration:none;'>🚪 تسجيل الخروج</a></div>"
       << "<h2>💼 لوحة تحكم الإدارة العليا والمسح</h2>"
       << "<div class='table-container'><table><thead><tr><th>رقم</th><th>اسم العميل</th><th>النظام</th><th>الأدوار</th><th>الحالة</th><th>الإجراءات المتاحة للمدير فقط</th></tr></thead><tbody>";

    for (const auto& insp : list) {
        os << "<tr><td>" << insp.id << "</td>"
           << "<td><b>" << html_escape(insp.client_name) << "</b></td>"
           << "<td>" << html_escape(insp.m_type) << "</td>"
           << "<td>" << insp.floors << "</td>"
           << "<td><span style='color:#fd7e14;font-weight:bold;'> " << html_escape(insp.status) << "</span></td>"
           << "<td>"
           << "<a class='btn' href='/calculate?id=" << insp.id << "'>📊 مراجعة وحساب</a>"
           << "<a class='btn-del' href='/delete?id=" << insp.id << "' onclick='return confirm(\"هل أنت متأكد من مسح وإلغاء هذه المعاينة كلياً؟\")'>❌ مسح نهائي</a>"
           << "</td></tr>";
    }
    os << "</tbody></table></div><br><a href='/'>← شاشة الفني</a></div></body></html>";
    return os.str();
}

int main() {
    init_database();
    httplib::Server svr;
    Elevator elevator;

    // شاشة الفني الرئيسية
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        string html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><style>"
                      "body{background:#f0f2f5;font-family:sans-serif;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0;padding:20px;box-sizing:border-box;flex-direction:column;}"
                      ".card{background:white;padding:25px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);width:100%;max-width:400px;direction:rtl;text-align:right;box-sizing:border-box;}"
                      "h2{color:#28a745;text-align:center;margin-bottom:15px;}.f-group{margin-bottom:10px;}"
                      "label{font-weight:600;color:#495057;display:block;margin-bottom:4px;font-size:14px;}"
                      "input,select{width:100%;padding:8px;border:1px solid #ced4da;border-radius:6px;box-sizing:border-box;text-align:center;font-size:16px;background:#f8f9fa;}"
                      "button{background:#007bff;color:white;border:none;padding:12px;border-radius:6px;width:100%;font-size:16px;font-weight:bold;cursor:pointer;margin-top:10px;}"
                      ".nav-links{display:flex;justify-content:space-between;width:100%;max-width:400px;margin-top:15px;direction:rtl;}"
                      ".nav-links a{color:#007bff;text-decoration:none;font-size:15px;font-weight:bold;}"
                      "</style></head><body><div class='card'><h2>👷‍♂️ لوحة الفني: الشعراوي بيمسي</h2>"
                      "<form action='/save' method='get'>"
                      "<div class='f-group'><label>اسم العميل:</label><input type='text' name='c_name' required maxlength='200' placeholder='اسم العميل '></div>"
                      "<div class='f-group'><label>نوع النظام:</label><select name='m_type'><option value='MR'>بغرفة محرك (MR)</option><option value='MRL'>بدون غرفة محرك (MRL)</option></select></div>"
                      "<div class='f-group'><label>1. عرض البئر (CM):</label><input type='number' name='width' required min='1' max='1000'></div>"
                      "<div class='f-group'><label>2. عمق البئر (CM):</label><input type='number' name='depth' required min='1' max='1000'></div>"
                      "<div class='f-group'><label>3. عدد الأدوار:</label><input type='number' name='floors' required min='1' max='200'></div>"
                      "<div class='f-group'><label>4. عمق الحفرة (CM):</label><input type='number' name='pit' required min='0' max='1000'></div>"
                      "<div class='f-group'><label>5. الارتفاع العلوي (CM):</label><input type='number' name='overhead' required min='0' max='1000'></div>"
                      "<button type='submit'> احفظ المعاينة يافخم</button></form></div>"
                      "<div class='nav-links'><a href='/tech-view'> المعاينات السابقة</a>"
                      "<a href='/admin-login'> اتفضل يامدير💚 ←</a></div></body></html>";
        res.set_content(html, "text/html; charset=utf-8");
    });

    // حفظ المعاينة بأمان (مع فحص كامل للمدخلات)
    svr.Get("/save", [](const httplib::Request& req, httplib::Response& res) {
        string name = req.get_param_value("c_name");
        string m_type = req.get_param_value("m_type");

        if (name.empty() || name.length() > 200) {
            res.set_content(error_page("اسم العميل غير صحيح"), "text/html; charset=utf-8");
            return;
        }
        if (m_type != "MR" && m_type != "MRL") {
            res.set_content(error_page("نوع النظام غير صحيح"), "text/html; charset=utf-8");
            return;
        }

        int width, depth, pit, overhead;
        float floors;
        if (!parse_int_safe(req.get_param_value("width"), width, 1, 1000) ||
            !parse_int_safe(req.get_param_value("depth"), depth, 1, 1000) ||
            !parse_float_safe(req.get_param_value("floors"), floors, 1, 200) ||
            !parse_int_safe(req.get_param_value("pit"), pit, 0, 1000) ||
            !parse_int_safe(req.get_param_value("overhead"), overhead, 0, 1000)) {
            res.set_content(error_page("من فضلك تأكد من إدخال أرقام صحيحة ومنطقية في كل الحقول"), "text/html; charset=utf-8");
            return;
        }

        string status = "قيد المراجعة";
        bool saved = false;

        PGconn* conn = connect_db();
        if (PQstatus(conn) == CONNECTION_OK) {
            string width_s = to_string(width);
            string depth_s = to_string(depth);
            string floors_s = to_string(floors);
            string pit_s = to_string(pit);
            string overhead_s = to_string(overhead);
            const char* paramValues[] = { name.c_str(), m_type.c_str(), width_s.c_str(), depth_s.c_str(),
                                           floors_s.c_str(), pit_s.c_str(), overhead_s.c_str(), status.c_str() };
            string query = "INSERT INTO inspections (client_name, m_type, width, depth, floors, pit, overhead, status) VALUES ($1, $2, $3, $4, $5, $6, $7, $8);";
            PGresult* insert_res = PQexecParams(conn, query.c_str(), 8, NULL, paramValues, NULL, NULL, 0);
            if (PQresultStatus(insert_res) == PGRES_COMMAND_OK) {
                saved = true;
            } else {
                cerr << "فشل حفظ المعاينة: " << PQresultErrorMessage(insert_res) << endl;
            }
            PQclear(insert_res);
        }
        PQfinish(conn);

        if (!saved) {
            res.set_content(error_page("حدث خطأ أثناء حفظ المعاينة، حاول مرة أخرى"), "text/html; charset=utf-8");
            return;
        }

        string success = "<html><head><meta charset='UTF-8'></head><body style='font-family:sans-serif; text-align:center; padding-top:50px; direction:rtl;'>"
                         "<h2 style='color:#28a745;'>البتاع اتحفظ ياخويا </h2>"
                         "<p>بيانات العميل ☺️ (<b>" + html_escape(name) + "</b>) سر كبير ومش هيضيع خالص😂 .</p>"
                         "<br><a href='/' style='background:#007bff; color:white; padding:10px 20px; text-decoration:none; border-radius:5px;'>إضافة معاينة جديدة</a>"
                         "</body></html>";
        res.set_content(success, "text/html; charset=utf-8");
    });

    // عرض الفني للمعاينات السابقة (قراءة فقط، بدون أي صلاحية حذف أو اعتماد)
    svr.Get("/tech-view", [](const httplib::Request&, httplib::Response& res) {
        vector<Inspection> list;
        PGconn* conn = connect_db();
        if (PQstatus(conn) == CONNECTION_OK) {
            string query = "SELECT id, client_name, m_type, width, depth, floors, status FROM inspections ORDER BY id DESC;";
            PGresult* query_res = PQexec(conn, query.c_str());
            if (PQresultStatus(query_res) == PGRES_TUPLES_OK) {
                int rows = PQntuples(query_res);
                for (int i = 0; i < rows; i++) {
                    Inspection insp;
                    insp.id = stoi(PQgetvalue(query_res, i, 0));
                    insp.client_name = PQgetvalue(query_res, i, 1);
                    insp.m_type = PQgetvalue(query_res, i, 2);
                    insp.width = stoi(PQgetvalue(query_res, i, 3));
                    insp.depth = stoi(PQgetvalue(query_res, i, 4));
                    insp.floors = stof(PQgetvalue(query_res, i, 5));
                    insp.status = PQgetvalue(query_res, i, 6);
                    list.push_back(insp);
                }
            }
            PQclear(query_res);
        }
        PQfinish(conn);

        ostringstream os;
        os << "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><style>"
           << "body{background:#f0f2f5;font-family:sans-serif;padding:20px;direction:rtl;text-align:right;}"
           << ".box{max-width:800px;margin:auto;background:white;padding:20px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);}"
           << "h2{color:#28a745;text-align:center;}table{width:100%;border-collapse:collapse;margin-top:20px;text-align:center;}"
           << "th,td{padding:12px;border-bottom:1px solid #dee2e6;}th{background:#343a40;color:white;}"
           << ".btn{background:#17a2b8;color:white;text-decoration:none;padding:6px 12px;border-radius:4px;font-size:14px;}"
           << ".table-container{width:100%; overflow-x:auto;}"
           << "</style></head><body><div class='box'><h2>📋 راجع ع المعاينة ياخويا قبل ميتخصم عليك</h2>"
           << "<p style='color:#6c757d;'>تنويه: يمكنك فقط الاطلاع على المقاسات والتقارير ولا تملك صلاحية الحذف.</p>"
           << "<div class='table-container'><table><thead><tr><th>رقم</th><th>اسم العميل</th><th>النظام</th><th>الأدوار</th><th>الحالة</th><th>تقرير المقاسات</th></tr></thead><tbody>";

        for (const auto& insp : list) {
            os << "<tr><td>" << insp.id << "</td>"
               << "<td><b>" << html_escape(insp.client_name) << "</b></td>"
               << "<td>" << html_escape(insp.m_type) << "</td>"
               << "<td>" << insp.floors << "</td>"
               << "<td><span style='color:#007bff;font-weight:bold;'>" << html_escape(insp.status) << "</span></td>"
               << "<td><a class='btn' href='/calculate?id=" << insp.id << "'>🔍 عرض المقايسة</a></td></tr>";
        }
        os << "</tbody></table></div><br><a href='/'>🔙 العودة لشاشة الإدخال</a></div></body></html>";
        res.set_content(os.str(), "text/html; charset=utf-8");
    });

    // صفحة دخول المدير
    svr.Get("/admin-login", [](const httplib::Request&, httplib::Response& res) {
        string html = "<html><head><meta charset='UTF-8'><style>"
                      "body{background:#f0f2f5;font-family:sans-serif;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0;}"
                      ".card{background:white;padding:25px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);width:100%;max-width:350px;direction:rtl;text-align:center;}"
                      "input{width:100%;padding:10px;margin:10px 0;border:1px solid #ced4da;border-radius:6px;box-sizing:border-box;font-size:16px;text-align:center;}"
                      "button{background:#343a40;color:white;border:none;padding:12px;border-radius:6px;width:100%;font-size:16px;cursor:pointer;}"
                      "</style></head><body><div class='card'><h2>💼 دخول المدير</h2>"
                      "<form action='/admin' method='post'>"
                      "<input type='password' name='password' placeholder='أدخل الرقم السري' required>"
                      "<button type='submit'>دخول لوحة التحكم والمسح</button></form></div></body></html>";
        res.set_content(html, "text/html; charset=utf-8");
    });

    // دخول المدير الفعلي: تحقق من كلمة السر بزمن ثابت + إنشاء جلسة آمنة
    svr.Post("/admin", [](const httplib::Request& req, httplib::Response& res) {
        string pass = req.get_param_value("password");

        // تأخير بسيط لإبطاء هجمات تخمين كلمة السر المتكررة
        this_thread::sleep_for(milliseconds(300));

        if (!secure_compare(pass, ADMIN_PASSWORD)) {
            res.set_content(error_page("الرقم السري خاطئ"), "text/html; charset=utf-8");
            return;
        }

        string token = create_session();
        res.set_header("Set-Cookie", build_session_cookie(token));
        res.set_content(render_admin_dashboard(), "text/html; charset=utf-8");
    });

    // لو حد فاتح /admin مباشرة: يدخل اللوحة لو معاه جلسة صالحة، وإلا يتحول لصفحة الدخول
    svr.Get("/admin", [](const httplib::Request& req, httplib::Response& res) {
        if (is_admin_authenticated(req)) {
            res.set_content(render_admin_dashboard(), "text/html; charset=utf-8");
        } else {
            res.set_redirect("/admin-login");
        }
    });

    // تسجيل الخروج: إلغاء الجلسة من الذاكرة ومسح الكوكي
    svr.Get("/admin-logout", [](const httplib::Request& req, httplib::Response& res) {
        string token = get_cookie_value(req, "session_token");
        if (!token.empty()) destroy_session(token);
        res.set_header("Set-Cookie", build_logout_cookie());
        res.set_redirect("/admin-login");
    });

    // الحذف: محمي بالكامل، لا يعمل إلا لمن معه جلسة مدير صالحة
    svr.Get("/delete", [](const httplib::Request& req, httplib::Response& res) {
        if (!is_admin_authenticated(req)) {
            res.set_content(error_page("غير مسموح، يجب تسجيل الدخول كمدير أولاً"), "text/html; charset=utf-8");
            return;
        }

        int target_id;
        if (!parse_int_safe(req.get_param_value("id"), target_id, 1, 2000000000)) {
            res.set_content(error_page("رقم المعاينة غير صحيح"), "text/html; charset=utf-8");
            return;
        }

        PGconn* conn = connect_db();
        if (PQstatus(conn) == CONNECTION_OK) {
            string id_str = to_string(target_id);
            const char* paramValues[] = { id_str.c_str() };
            string delete_query = "DELETE FROM inspections WHERE id = $1;";
            PGresult* d_res = PQexecParams(conn, delete_query.c_str(), 1, NULL, paramValues, NULL, NULL, 0);
            if (PQresultStatus(d_res) != PGRES_COMMAND_OK) {
                cerr << "فشل الحذف: " << PQresultErrorMessage(d_res) << endl;
            }
            PQclear(d_res);
        }
        PQfinish(conn);
        res.set_redirect("/admin");
    });

    // الحساب والمراجعة: عرض مسموح للجميع (فني أو مدير)، لكن "اعتماد المعاينة"
    // لا يحدث إلا إذا كان الطلب من جلسة مدير حقيقية (لا اعتماد على باراميتر role أبداً)
    svr.Get("/calculate", [&elevator](const httplib::Request& req, httplib::Response& res) {
        int target_id;
        if (!parse_int_safe(req.get_param_value("id"), target_id, 1, 2000000000)) {
            res.set_content(error_page("رقم المعاينة غير صحيح"), "text/html; charset=utf-8");
            return;
        }

        bool is_admin = is_admin_authenticated(req);
        Inspection insp;
        bool found = false;

        PGconn* conn = connect_db();
        if (PQstatus(conn) == CONNECTION_OK) {
            string id_str = to_string(target_id);
            const char* paramValues[] = { id_str.c_str() };

            if (is_admin) {
                string update_query = "UPDATE inspections SET status = 'تمت المراجعة والاعتماد' WHERE id = $1;";
                PGresult* u_res = PQexecParams(conn, update_query.c_str(), 1, NULL, paramValues, NULL, NULL, 0);
                if (PQresultStatus(u_res) != PGRES_COMMAND_OK) {
                    cerr << "فشل تحديث الحالة: " << PQresultErrorMessage(u_res) << endl;
                }
                PQclear(u_res);
            }

            string query = "SELECT client_name, m_type, width, depth, floors FROM inspections WHERE id = $1;";
            PGresult* s_res = PQexecParams(conn, query.c_str(), 1, NULL, paramValues, NULL, NULL, 0);
            if (PQresultStatus(s_res) == PGRES_TUPLES_OK && PQntuples(s_res) > 0) {
                insp.client_name = PQgetvalue(s_res, 0, 0);
                insp.m_type = PQgetvalue(s_res, 0, 1);
                insp.width = stoi(PQgetvalue(s_res, 0, 2));
                insp.depth = stoi(PQgetvalue(s_res, 0, 3));
                insp.floors = stof(PQgetvalue(s_res, 0, 4));
                found = true;
            }
            PQclear(s_res);
        }
        PQfinish(conn);

        if (!found) {
            res.set_content(error_page("لم يتم العثور على هذه المعاينة"), "text/html; charset=utf-8");
            return;
        }

        string m_type = insp.m_type;
        int w = insp.width;
        int d = insp.depth;
        float f = insp.floors;

        string door = elevator.get_door_type(w);
        int cabin_dbg = elevator.get_cabin_dbg(w);
        int cwt_dbg = elevator.get_cwt_dbg(w);
        int cab_w = elevator.get_cabin_width(w);
        int cab_d = elevator.get_cabin_depth(d);
        float h = elevator.get_shaft_height(f, m_type);

        int brackets = elevator.calc_brackets(h);
        int bolts = elevator.calc_bolts(brackets);
        float ropes = elevator.calc_ropes(h);
        int fishplates = ((int)f) * 4;

        float c_brackets = brackets * elevator.get_p_bracket();
        float c_bolts = bolts * elevator.get_p_bolt();
        float c_ropes = ropes * elevator.get_p_rope();
        float c_fishplates = fishplates * elevator.get_p_fish();
        float total = c_brackets + c_bolts + c_ropes + c_fishplates;

        ostringstream os;
        os << "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><style>"
           << "body{background:#f0f2f5;font-family:sans-serif;padding:20px;direction:rtl;text-align:right;}"
           << ".box{max-width:600px;margin:auto;background:white;padding:20px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);}"
           << "h2{color:#007bff;text-align:center;}h3{color:#495057;border-bottom:2px solid #dee2e6;padding-bottom:4px;}"
           << ".tbl{width:100%;border-collapse:collapse;margin-top:10px;direction:ltr;text-align:left;}"
           << ".tbl th{background:#f8f9fa;padding:8px;border-bottom:1px solid #dee2e6;width:35%;}"
           << ".tbl td{padding:8px;border-bottom:1px solid #dee2e6;}"
           << ".btbl{width:100%;border-collapse:collapse;margin-top:10px;text-align:center;}"
           << ".btbl th{background:#343a40;color:white;padding:8px;}.btbl td{padding:8px;border-bottom:1px solid #dee2e6;}"
           << ".inv{background:#e2f0d9;padding:15px;border-radius:8px;border:2px dashed #385723;margin-top:15px;text-align:center;font-size:18px;font-weight:bold;color:#385723;}"
           << ".table-container{width:100%; overflow-x:auto;}"
           << "</style></head><body><div class='box'><h2>📋 تقرير مراجعة المقايسة المعتمدة سحابياً</h2>"
           << "<p style='text-align:center;font-weight:bold;font-size:18px;color:#2b5797;'>العميل: " << html_escape(insp.client_name) << "</p>"
           << "<h3>📐 أولاً: الأبعاد الهندسية</h3>"
           << "<div class='table-container'><table class='tbl'>"
           << "<tr><th>* Door Type:</th><td>" << door << "</td></tr>"
           << "<tr><th>* Cabin DBG:</th><td><b>" << cabin_dbg << " CM</b></td></tr>"
           << "<tr><th>* CWT DBG:</th><td><b>" << (cwt_dbg == 0 ? "Review Official" : to_string(cwt_dbg) + " CM") << "</b></td></tr>"
           << "<tr><th>* Cabin Width:</th><td><b>" << cab_w << " CM</b></td></tr>"
           << "<tr><th>* Cabin Depth:</th><td><b>" << cab_d << " CM</b></td></tr>"
           << "<tr><th>* Shaft Height:</th><td style='color:#fd7e14;font-weight:bold;'>" << h << " Meters</td></tr>"
           << "</table></div>"
           << "<h3>📦 ثانياً: البضاعة والتكلفة المالية المحسوبة</h3>"
           << "<div class='table-container'><table class='btbl'><thead><tr><th>اسم الصنف</th><th>الكمية</th><th> التكلفة</th></tr></thead><tbody>"
           << "<tr><td>كوابيل السكك</td><td>" << brackets << " 🛑</td><td>" << c_brackets << " EGP</td></tr>"
           << "<tr><td>مسامير التثبيت</td><td>" << bolts << " 🔩</td><td>" << c_bolts << " EGP</td></tr>"
           << "<tr><td>حبال الواير</td><td>" << ropes << " 🧵</td><td>" << c_ropes << " EGP</td></tr>"
           << "<tr><td>لقم السكك</td><td>" << fishplates << " 🗜️</td><td>" << c_fishplates << " EGP</td></tr>"
           << "</tbody></table></div>"
           << "<div class='inv'>💰 إجمالي تكلفة البضاعة: " << total << " EGP</div>";
        if (is_admin) {
            os << "<center><a href='/admin' style='display:inline-block;margin-top:15px;color:#007bff;text-decoration:none;'>🔙 العودة لجدول المعاينات للمدير</a></center>";
        } else {
            os << "<center><a href='/tech-view' style='display:inline-block;margin-top:15px;color:#007bff;text-decoration:none;'>🔙 العودة لجدول الفني</a></center>";
        }
        os << "</div></body></html>";
        res.set_content(os.str(), "text/html; charset=utf-8");
    });

    const char* port_env = getenv("PORT");
    int port = port_env ? stoi(port_env) : 8080;
    svr.listen("0.0.0.0", port);
    return 0;
}
