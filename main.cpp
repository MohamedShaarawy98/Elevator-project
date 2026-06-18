#include "httplib.h" 
#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <map>

using namespace std;

// بنية بيانات لتخزين معلومات العميل والمعاينة
struct Inspection {
    int id;
    string client_name;
    string m_type;
    int width;
    int depth;
    float floors;
    int pit;
    int overhead;
    string status; // "قيد المراجعة" أو "تمت المراجعة"
};

// قاعدة بيانات وهمية في الذاكرة (للتوضيح وسهولة التشغيل الفوري على Render)
// ملاحظة: عند إعادة تشغيل السيرفر في ريندر تختفي البيانات، لضمان الحفظ الدائم يفضل ربطها بملف SQLite لاحقاً
vector<Inspection> db;
int next_id = 1;

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

    // 1. واجهة الفني لرفع المقاسات وإدخال اسم العميل
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
                      "<a href='/admin'>💼 دخول مدير التركيبات ←</a></div></body></html>";
        res.set_content(html, "text/html; charset=utf-8");
    });

    // 2. معالجة الحفظ التلقائي للمعاينات
    svr.Get("/save", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("c_name") || !req.has_param("width") || !req.has_param("depth")) {
            res.set_content("خطأ في البيانات المرسلة", "text/plain; charset=utf-8");
            return;
        }

        Inspection insp;
        insp.id = next_id++;
        insp.client_name = req.get_param_value("c_name");
        insp.m_type = req.get_param_value("m_type");
        insp.width = stoi(req.get_param_value("width"));
        insp.depth = stoi(req.get_param_value("depth"));
        insp.floors = stof(req.get_param_value("floors"));
        insp.pit = stoi(req.get_param_value("pit"));
        insp.overhead = stoi(req.get_param_value("overhead"));
        insp.status = "قيد المراجعة";

        db.push_back(insp);

        string success_html = "<html><head><meta charset='UTF-8'></head><body style='font-family:sans-serif; text-align:center; padding-top:50px; direction:rtl;'>"
                              "<h2 style='color:#28a745;'>✅ تم حفظ المقاسات بنجاح!</h2>"
                              "<p>تم إرسال معاينة العميل (<b>" + insp.client_name + "</b>) إلى مدير التركيبات للمراجعة الفنية والمالية.</p>"
                              "<br><a href='/' style='background:#007bff; color:white; padding:10px 20px; text-decoration:none; border-radius:5px;'>إضافة معاينة جديدة</a>"
                              "</body></html>";
        res.set_content(success_html, "text/html; charset=utf-8");
    });

    // 3. لوحة تحكم مدير التركيبات لمراجعة الطلبات واختيار العميل
    svr.Get("/admin", [](const httplib::Request&, httplib::Response& res) {
        ostringstream os;
        os << "<html><head><meta charset='UTF-8'><style>"
           << "body{background:#f0f2f5;font-family:sans-serif;padding:20px;direction:rtl;text-align:right;}"
           << ".box{max-width:800px;margin:auto;background:white;padding:20px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.08);}"
           << "h2{color:#007bff;text-align:center;}table{width:100%;border-collapse:collapse;margin-top:20px;text-align:center;}"
           << "th,td{padding:12px;border-bottom:1px solid #dee2e6;}th{background:#343a40;color:white;}"
           << ".btn{background:#28a745;color:white;text-decoration:none;padding:6px 12px;border-radius:4px;font-size:14px;font-weight:bold;}"
           << ".badge{background:#ffc107;color:#212529;padding:4px 8px;border-radius:12px;font-size:12px;font-weight:bold;}"
           << ".back-lnk{display:inline-block;margin-top:15px;color:#007bff;text-decoration:none;}"
           << "</style></head><body><div class='box'><h2>💼 لوحة تحكم مدير التركيبات</h2>"
           << "<p>مراجعة تقارير الفنيين للمواقع والمقايسات الجارية:</p>";

        if(db.empty()) {
            os << "<p style='text-align:center;color:#6c757d;margin-top:30px;'>🚫 لا توجد معاينات مرفوعة حالياً.</p>";
        } else {
            os << "<table><thead><tr><th>رقم</th><th>اسم العميل</th><th>النظام</th><th>الأدوار</th><th>الحالة</th><th>الإجراء</th></tr></thead><tbody>";
            for(const auto& insp : db) {
                os << "<tr><td>" << insp.id << "</td>"
                   << "<td><b>" << insp.client_name << "</b></td>"
                   << "<td>" << (insp.m_type == "MRL" ? "MRL" : "MR") << "</td>"
                   << "<td>" << insp.floors << "</td>"
                   << "<td><span class='badge'>" << insp.status << "</span></td>"
                   << "<td><a class='btn' href='/calculate?id=" << insp.id << "'>📊 مراجعة وحساب</a></td></tr>";
            }
            os << "</tbody></table>";
        }
        os << "<br><a href='/' class='back-lnk'>← العودة لشاشة الفني</a></div></body></html>";
        res.set_content(os.str(), "text/html; charset=utf-8");
    });

    // 4. صفحة عرض النتائج التفصيلية والفاتورة للمدير
    svr.Get("/calculate", [&elevator](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("id")) {
            res.set_content("خطأ: لم يتم تحديد معرف العميل", "text/plain; charset=utf-8");
            return;
        }
        
        int target_id = stoi(req.get_param_value("id"));
        Inspection* current_insp = nullptr;
        
        // البحث عن العميل المحدد في قاعدة البيانات
        for(auto& insp : db) {
            if(insp.id == target_id) {
                current_insp = &insp;
                break;
            }
        }

        if(!current_insp) {
            res.set_content("العميل غير موجود", "text/plain; charset=utf-8");
            return;
        }

        // تحديث حالة المعاينة لأن المدير راجعها الآن
        current_insp->status = "تمت المراجعة المعمارية";

        string m_type = current_insp->m_type;
        int w = current_insp->width;
        int d = current_insp->depth;
        float f = current_insp->floors;

        string door = elevator.get_door_type(w);
