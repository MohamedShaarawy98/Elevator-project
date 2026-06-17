#include "httplib.h" 
#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib> // مكتبة قراءة متغيرات السيرفر السحابي

using namespace std;

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

int main() {
    httplib::Server svr;
    Elevator elevator;

    // واجهة الإدخال
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        string html = "<html><head><meta charset='UTF-8'><style>"
                      "body{background:#f0f2f5;font-family:sans-serif;display:flex;align-items:center;justify-content:center;min-vh-100;margin:0;}"
                      ".card{background:white;padding:25px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);width:100%;max-width:400px;direction:rtl;text-align:right;box-sizing:border-box;}"
                      "h2{color:#007bff;text-align:center;margin-bottom:15px;}.f-group{margin-bottom:10px;}"
                      "label{font-weight:600;color:#495057;display:block;margin-bottom:4px;font-size:14px;}"
                      "input,select{width:100%;padding:8px;border:1px solid #ced4da;border-radius:6px;box-sizing:border-box;text-align:center;font-size:16px;background:#f8f9fa;}"
                      "button{background:#28a745;color:white;border:none;padding:12px;border-radius:6px;width:100%;font-size:16px;font-weight:bold;cursor:pointer;margin-top:10px;}"
                      "</style></head><body><div class='card'><h1>🛠️ الشعراوي بيمسي عليكم</h1>"
                      "<form action='/calculate' method='get'>"
                      "<div class='f-group'><label>نوع النظام:</label><select name='m_type'><option value='MR'>بغرفة محرك (MR)</option><option value='MRL'>بدون غرفة محرك (MRL)</option></select></div>"
                      "<div class='f-group'><label>1. عرض البئر (CM):</label><input type='number' name='width' required></div>"
                      "<div class='f-group'><label>2. عمق البئر (CM):</label><input type='number' name='depth' required></div>"
                      "<div class='f-group'><label>3. عدد الأدوار:</label><input type='number' name='floors' required></div>"
                      "<div class='f-group'><label>4. عمق الحفرة (CM):</label><input type='number' name='pit' required></div>"
                      "<div class='f-group'><label>5. الارتفاع العلوي (CM):</label><input type='number' name='overhead' required></div>"
                      "<button type='submit'>📊 doun</button></form></div></body></html>";
        res.set_content(html, "text/html; charset=utf-8");
    });

    // صفحة عرض النتائج والفاتورة
    svr.Get("/calculate", [&elevator](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("width") || !req.has_param("depth") || !req.has_param("floors") || !req.has_param("m_type")) {
            res.set_content("بيانات ناقصة", "text/plain; charset=utf-8");
            return;
        }
        string m_type = req.get_param_value("m_type");
        int w = stoi(req.get_param_value("width"));
        int d = stoi(req.get_param_value("depth"));
        float f = stof(req.get_param_value("floors"));

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
           << "h2{color:#007bff;text-align:center;}h3{color:#495057;border-bottom:2px solid #dee2e6;padding-bottom:4px;margin-top:15px;}"
           << ".tbl{width:100%;border-collapse:collapse;margin-top:10px;direction:ltr;text-align:left;}"
           << ".tbl th{background:#f8f9fa;padding:8px;border-bottom:1px solid #dee2e6;width:35%;}"
           << ".tbl td{padding:8px;border-bottom:1px solid #dee2e6;}"
           << ".btbl{width:100%;border-collapse:collapse;margin-top:10px;text-align:center;}"
           << ".btbl th{background:#343a40;color:white;padding:8px;}.btbl td{padding:8px;border-bottom:1px solid #dee2e6;}"
           << ".badge{background:#28a745;color:white;padding:4px 10px;border-radius:20px;font-weight:bold;}"
           << ".inv{background:#e2f0d9;padding:15px;border-radius:8px;border:2px dashed #385723;margin-top:15px;text-align:center;font-size:18px;font-weight:bold;color:#385723;}"
           << ".lnk{display:inline-block;background:#007bff;color:white;text-decoration:none;padding:10px 20px;border-radius:6px;margin-top:15px;}"
           << "</style></head><body><div class='box'><h2>📋 تقرير المقايسة والفاتورة</h2>"
           << "<p style='text-align:center;font-weight:bold;'>النظام: " << (m_type == "MRL" ? "بدون غرفة (MRL)" : "بغرفة (MR)") << "</p>"
           << "<h3>📐 أولاً: المقاسات والأبعاد</h3>"
           << "<table class='tbl'>"
           << "<tr><th>* Door Type:</th><td>" << door << "</td></tr>"
           << "<tr><th>* Cabin DBG:</th><td><b>" << cabin_dbg << " CM</b></td></tr>"
           << "<tr><th>* CWT DBG:</th><td><b>" << (cwt_dbg == 0 ? "Review Official" : to_string(cwt_dbg) + " CM") << "</b></td></tr>"
           << "<tr><th>* Cabin Width:</th><td><b>" << cab_w << " CM</b></td></tr>"
           << "<tr><th>* Cabin Depth:</th><td><b>" << cab_d << " CM</b></td></tr>"
           << "<tr><th>* Shaft Height:</th><td style='color:#fd7e14;font-weight:bold;'>" << h << " Meters</td></tr>"
           << "</table>"
           << "<h3>📦 ثانياً: البضاعة والتكلفة المالية</h3>"
           << "<table class='btbl'><thead><tr style='background:#343a40;color:white;'><th>اسم الصنف</th><th>الكمية</th><th> التكلفة</th></tr></thead><tbody>"
           << "<tr><td style='text-align:right;padding-right:10px;'>كوابيل السكك</td><td><span class='badge'>" << brackets << " قطعة</span></td><td>" << c_brackets << "</td></tr>"
           << "<tr><td style='text-align:right;padding-right:10px;'>مسامير التثبيت</td><td><span class='badge'>" << bolts << " مسمار</span></td><td>" << c_bolts << "</td></tr>"
           << "<tr><td style='text-align:right;padding-right:10px;'>حبال الوايرات</td><td><span class='badge'>" << ropes << " متر</span></td><td>" << c_ropes << "</td></tr>"
           << "<tr><td style='text-align:right;padding-right:10px;'>لقم وصل السكك</td><td><span class='badge'>" << fishplates << " قطعة</span></td><td>" << c_fishplates << "</td></tr>"
           << "</tbody></table>"
           << "<div class='inv'>💰 إجمالي تكلفة البضاعة = " << total << " جنيه / ريال </div>"
           << "<div style='text-align:center;'><a href='/' class='lnk'>🔄 حساب مقايسة جديدة</a></div></div></body></html>";

        res.set_content(os.str(), "text/html; charset=utf-8");
    });

    // التعديل السحابي: قراءة البورت المتاح مجاناً من السيرفر تلقائياً
    const char* port_env = getenv("PORT");
    int port = port_env ? stoi(port_env) : 8080;
    
    cout << "Server successfully started at port " << port << endl;
    svr.listen("0.0.0.0", port);
    return 0;
}
