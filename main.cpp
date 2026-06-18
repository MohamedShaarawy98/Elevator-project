#include "httplib.h" 
#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <postgresql/libpq-fe.h>

using namespace std;

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

const string ADMIN_PASSWORD = "135790"; 

class Elevator {
private:
    const float P_BRACKET = 0.0;  
    const float P_BOLT = 0.0;      
    const float P_ROPE = 0.0;
    const float P_FISH = 0.0;

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
        PQclear(res);
    }
    PQfinish(conn);
}
int main() {
    init_database(); 
    httplib::Server svr;
    Elevator elevator;

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        string html = "<html><head><meta charset='UTF-8'><style>"
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
                      "<div class='f-group'><label>اسم العميل:</label><input type='text' name='c_name' required placeholder='اسم العميل '></div>"
                      "<div class='f-group'><label>نوع النظام:</label><select name='m_type'><option value='MR'>بغرفة محرك (MR)</option><option value='MRL'>بدون غرفة محرك (MRL)</option></select></div>"
                      "<div class='f-group'><label>1. عرض البئر (CM):</label><input type='number' name='width' required></div>"
                      "<div class='f-group'><label>2. عمق البئر (CM):</label><input type='number' name='depth' required></div>"
                      "<div class='f-group'><label>3. عدد الأدوار:</label><input type='number' name='floors' required></div>"
                      "<div class='f-group'><label>4. عمق الحفرة (CM):</label><input type='number' name='pit' required></div>"
                      "<div class='f-group'><label>5. الارتفاع العلوي (CM):</label><input type='number' name='overhead' required></div>"
                      "<button type='submit'> احفظ المعاينة يافخم</button></form></div>"
                      "<div class='nav-links'><a href='/tech-view'> المعاينات السابقة</a>"
                      "<a href='/admin-login'> اتفضل يامدير💚 ←</a></div></body></html>";
        res.set_content(html, "text/html; charset=utf-8");
    });

    svr.Get("/save", [](const httplib::Request& req, httplib::Response& res) {
        string name = req.get_param_value("c_name");
        string m_type = req.get_param_value("m_type");
        string width = req.get_param_value("width");
        string depth = req.get_param_value("depth");
        string floors = req.get_param_value("floors");
        string pit = req.get_param_value("pit");
        string overhead = req.get_param_value("overhead");
        string status = "قيد المراجعة";

        PGconn* conn = connect_db();
        if (PQstatus(conn) == CONNECTION_OK) {
            const char* paramValues[] = { name.c_str(), m_type.c_str(), width.c_str(), depth.c_str(), floors.c_str(), pit.c_str(), overhead.c_str(), status.c_str() };
            string query = "INSERT INTO inspections (client_name, m_type, width, depth, floors, pit, overhead, status) VALUES ($1, $2, $3, $4, $5, $6, $7, $8);";
            PGresult* insert_res = PQexecParams(conn, query.c_str(), 8, NULL, paramValues, NULL, NULL, 0);
            PQclear(insert_res);
        }
        PQfinish(conn);

        string success = "<html><head><meta charset='UTF-8'></head><body style='font-family:sans-serif; text-align:center; padding-top:50px; direction:rtl;'>"
                         "<h2 style='color:#28a745;'>البتاع اتحفظ ياخويا </h2>"
                         "<p>بيانات العميل ☺️ (<b>" + name + "</b>) سر كبير ومش هيضيع خالص😂 .</p>"
                         "<br><a href='/' style='background:#007bff; color:white; padding:10px 20px; text-decoration:none; border-radius:5px;'>إضافة معاينة جديدة</a>"
                         "</body></html>";
        res.set_content(success, "text/html; charset=utf-8");
    });




    svr.Get("/tech-view", [](const httplib::Request&, httplib::Response& res) {
        vector<Inspection> list;
        PGconn* conn = connect_db();
        if (PQstatus(conn) == CONNECTION_OK) {
            string query = "SELECT id, client_name, m_type, width, depth, floors, status FROM inspections ORDER BY id DESC;";
            PGresult* query_res = PQexec(conn, query.c_str());
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
            PQclear(query_res);
        }
        PQfinish(conn);

        ostringstream os;
        os << "<html><head><meta charset='UTF-8'><style>"
           << "body{background:#f0f2f5;font-family:sans-serif;padding:20px;direction:rtl;text-align:right;}"
           << ".box{max-width:800px;margin:auto;background:white;padding:20px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);}"
           << "h2{color:#28a745;text-align:center;}table{width:100%;border-collapse:collapse;margin-top:20px;text-align:center;}"
           << "th,td{padding:12px;border-bottom:1px solid #dee2e6;}th{background:#343a40;color:white;}"
           << ".btn{background:#17a2b8;color:white;text-decoration:none;padding:6px 12px;border-radius:4px;font-size:14px;}"
           << "</style></head><body><div class='box'><h2>📋 راجع ع المعاينة ياخويا قبل ميتخصم عليك</h2>"
           << "<p style='color:#6c757d;'>تنويه: يمكنك فقط الاطلاع على المقاسات والتقارير ولا تملك صلاحية الحذف.</p>"
           << "<table><thead><tr><th>رقم</th><th>اسم العميل</th><th>النظام</th><th>الأدوار</th><th>الحالة</th><th>تقرير المقاسات</th></tr></thead><tbody>";

        for (const auto& insp : list) {
            os << "<tr><td>" << insp.id << "</td>"
               << "<td><b>" << insp.client_name << "</b></td>"
               << "<td>" << insp.m_type << "</td>"
               << "<td>" << insp.floors << "</td>"
               << "<td><span style='color:#007bff;font-weight:bold;'>" << insp.status << "</span></td>"
               << "<td><a class='btn' href='/calculate?id=" << insp.id << "&password=tech'>🔍 عرض المقايسة</a></td></tr>";
        }
        os << "</tbody></table><br><a href='/'>🔙 العودة لشاشة الإدخال</a></div></body></html>";
        res.set_content(os.str(), "text/html; charset=utf-8");
    });

    svr.Get("/admin-login", [](const httplib::Request&, httplib::Response& res) {
        string html = "<html><head><meta charset='UTF-8'><style>"
                      "body{background:#f0f2f5;font-family:sans-serif;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0;}"
                      ".card{background:white;padding:25px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);width:100%;max-width:350px;direction:rtl;text-align:center;}"
                      "input{width:100%;padding:10px;margin:10px 0;border:1px solid #ced4da;border-radius:6px;box-sizing:border-box;font-size:16px;text-align:center;}"
                      "button{background:#343a40;color:white;border:none;padding:12px;border-radius:6px;width:100%;font-size:16px;cursor:pointer;}"
                      "</style></head><body><div class='card'><h2>💼 دخول المدير</h2>"
                      "<form action='/admin' method='get'>"
                      "<input type='password' name='password' placeholder='أدخل الرقم السري' required>"
                      "<button type='submit'>دخول لوحة التحكم والمسح</button></form></div></body></html>";
        res.set_content(html, "text/html; charset=utf-8");
    });




    svr.Get("/admin", [](const httplib::Request& req, httplib::Response& res) {
        string pass = req.get_param_value("password");
        if (pass != ADMIN_PASSWORD) {
            res.set_content("<html><head><meta charset='UTF-8'></head><body style='text-align:center;padding-top:50px;'><h2>❌ الرقم السري خاطئ!</h2></body></html>", "text/html; charset=utf-8");
            return;
        }

        vector<Inspection> list;
        PGconn* conn = connect_db();
        if (PQstatus(conn) == CONNECTION_OK) {
            string query = "SELECT id, client_name, m_type, width, depth, floors, status FROM inspections ORDER BY id DESC;";
            PGresult* query_res = PQexec(conn, query.c_str());
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
            PQclear(query_res);
        }
        PQfinish(conn);

        ostringstream os;
        os << "<html><head><meta charset='UTF-8'><style>"
           << "body{background:#f0f2f5;font-family:sans-serif;padding:20px;direction:rtl;text-align:right;}"
           << ".box{max-width:850px;margin:auto;background:white;padding:20px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);}"
           << "h2{color:#007bff;text-align:center;}table{width:100%;border-collapse:collapse;margin-top:20px;text-align:center;}"
           << "th,td{padding:12px;border-bottom:1px solid #dee2e6;}th{background:#343a40;color:white;}"
           << ".btn{background:#28a745;color:white;text-decoration:none;padding:6px 12px;border-radius:4px;font-size:14px;margin-left:5px;display:inline-block;}"
           << ".btn-del{background:#dc3545;color:white;text-decoration:none;padding:6px 12px;border-radius:4px;font-size:14px;display:inline-block;}"
           << "</style></head><body><div class='box'><h2>💼 لوحة تحكم الإدارة العليا والمسح</h2>"
           << "<table><thead><tr><th>رقم</th><th>اسم العميل</th><th>النظام</th><th>الأدوار</th><th>الحالة</th><th>الإجراءات المتاحة للمدير فقط</th></tr></thead><tbody>";

        for (const auto& insp : list) {
            os << "<tr><td>" << insp.id << "</td>"
               << "<td><b>" << insp.client_name << "</b></td>"
               << "<td>" << insp.m_type << "</td>"
               << "<td>" << insp.floors << "</td>"
               << "<td><span style='color:#fd7e14;font-weight:bold;'> " << insp.status << "</span></td>"
               << "<td>"
               << "<a class='btn' href='/calculate?id=" << insp.id << "&password=" << pass << "'>📊 مراجعة وحساب</a>"
               << "<a class='btn-del' href='/delete?id=" << insp.id << "&password=" << pass << "' onclick='return confirm(\"هل أنت متأكد من مسح وإلغلة هذه المعاينة كلياً؟\")'>❌ مسح نهائي</a>"
               << "</td></tr>";
        }
        os << "</tbody></table><br><a href='/'>← شاشة الفني</a></div></body></html>";
        res.set_content(os.str(), "text/html; charset=utf-8");
    });

    svr.Get("/delete", [](const httplib::Request& req, httplib::Response& res) {
        string pass = req.get_param_value("password");
        if (pass != ADMIN_PASSWORD) {
            res.set_content("غير مسموح للفنيين بمسح المعاينات", "text/plain; charset=utf-8");
            return;
        }
        string target_id = req.get_param_value("id");
        PGconn* conn = connect_db();
        if (PQstatus(conn) == CONNECTION_OK) {
            string delete_query = "DELETE FROM inspections WHERE id = " + target_id + ";";
            PGresult* d_res = PQexec(conn, delete_query.c_str());
            PQclear(d_res);
        }
        PQfinish(conn);
        res.set_redirect("/admin?password=" + pass);
    });

    svr.Get("/calculate", [&elevator](const httplib::Request& req, httplib::Response& res) {
        string target_id = req.get_param_value("id");
        string pass = req.get_param_value("password");
        Inspection insp;

        PGconn* conn = connect_db();
        if (PQstatus(conn) == CONNECTION_OK) {
            if (pass != "tech") {
                string update_query = "UPDATE inspections SET status = 'تمت المراجعة والاعتماد' WHERE id = " + target_id + ";";
                PGresult* u_res = PQexec(conn, update_query.c_str());
                PQclear(u_res);
            }
            string query = "SELECT client_name, m_type, width, depth, floors FROM inspections WHERE id = " + target_id + ";";
            PGresult* s_res = PQexec(conn, query.c_str());
            if (PQntuples(s_res) > 0) {
                insp.client_name = PQgetvalue(s_res, 0, 0);
                insp.m_type = PQgetvalue(s_res, 0, 1);
                insp.width = stoi(PQgetvalue(s_res, 0, 2));
                insp.depth = stoi(PQgetvalue(s_res, 0, 3));
                insp.floors = stof(PQgetvalue(s_res, 0, 4));
            }
            PQclear(s_res);
        }
        PQfinish(conn);

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
        os << "<html><head><meta charset='UTF-8'><style>"
           << "body{background:#f0f2f5;font-family:sans-serif;padding:20px;direction:rtl;text-align:right;}"
           << ".box{max-width:600px;margin:auto;background:white;padding:20px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);}"
           << "h2{color:#007bff;text-align:center;}h3{color:#495057;border-bottom:2px solid #dee2e6;padding-bottom:4px;}"
           << ".tbl{width:100%;border-collapse:collapse;margin-top:10px;direction:ltr;text-align:left;}"
           << ".tbl th{background:#f8f9fa;padding:8px;border-bottom:1px solid #dee2e6;width:35%;}"
           << ".tbl td{padding:8px;border-bottom:1px solid #dee2e6;}"
           << ".btbl{width:100%;border-collapse:collapse;margin-top:10px;text-align:center;}"
           << ".btbl th{background:#343a40;color:white;padding:8px;}.btbl td{padding:8px;border-bottom:1px solid #dee2e6;}"
           << ".inv{background:#e2f0d9;padding:15px;border-radius:8px;border:2px dashed #385723;margin-top:15px;text-align:center;font-size:18px;font-weight:bold;color:#385723;}"
           << "</style></head><body><div class='box'><h2>📋 تقرير مراجعة المقايسة المعتمدة سحابياً</h2>"
           << "<p style='text-align:center;font-weight:bold;font-size:18px;color:#2b5797;'>العميل: " << insp.client_name << "</p>"
           << "<h3>📐 أولاً: الأبعاد الهندسية</h3>"
           << "<table class='tbl'>"
           << "<tr><th>* Door Type:</th><td>" << door << "</td></tr>"
           << "<tr><th>* Cabin DBG:</th><td><b>" << cabin_dbg << " CM</b></td></tr>"
           << "<tr><th>* CWT DBG:</th><td><b>" << (cwt_dbg == 0 ? "Review Official" : to_string(cwt_dbg) + " CM") << "</b></td></tr>"
           << "<tr><th>* Cabin Width:</th><td><b>" << cab_w << " CM</b></td></tr>"
           << "<tr><th>* Cabin Depth:</th><td><b>" << cab_d << " CM</b></td></tr>"
           << "<tr><th>* Shaft Height:</th><td style='color:#fd7e14;font-weight:bold;'>" << h << " Meters</td></tr>"
           << "</table>"
           << "<h3>📦 ثانياً: البضاعة والتكلفة المالية المحسوبة</h3>"
           << "<table class='btbl'><thead><tr><th>اسم الصنف</th><th>الكمية</th><th> التكلفة</th></tr></thead><tbody>"
           << "<tr><td>كوابيل السكك</td><td>" << brackets << " 🛑</td><td>" << c_brackets << " EGP</td></tr>"
           << "<tr><td>مسامير التثبيت</td><td>" << bolts << " 🔩</td><td>" << c_bolts << " EGP</td></tr>"
           << "<tr><td>حبال الواير</td><td>" << ropes << " 🧵</td><td>" << c_ropes << " EGP</td></tr>"
           << "<tr><td>لقم السكك</td><td>" << fishplates << " 🗜️</td><td>" << c_fishplates << " EGP</td></tr>"
           << "</tbody></table>"
           << "<div class='inv'>💰 إجمالي تكلفة البضاعة: " << total << " EGP</div>";
           if (pass == "tech") {
               os << "<center><a href='/tech-view' style='display:inline-block;margin-top:15px;color:#007bff;text-decoration:none;'>🔙 العودة لجدول الفني</a></center>";
           } else {
               os << "<center><a href='/admin?password=" << pass << "' style='display:inline-block;margin-top:15px;color:#007bff;text-decoration:none;'>🔙 العودة لجدول المعاينات للمدير</a></center>";
           }
           os << "</div></body></html>";
        res.set_content(os.str(), "text/html; charset=utf-8");
    });

    const char* port_env = getenv("PORT");
    int port = port_env ? stoi(port_env) : 8080;
    svr.listen("0.0.0.0", port);
    return 0;
}
