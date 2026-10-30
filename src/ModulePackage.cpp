#include "ModulePackage.hpp"
#include "NeoWxUi.hpp"
#include <gff/GFFFile.hpp>
#include <neoshared/bif/LooseArchiveCatalog.hpp>
#include <neoshared/erf/Archive.hpp>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/wrapsizer.h>
#include <algorithm>
#include <array>
#include <set>
#include <sstream>

namespace neotoolkit {
namespace {
using Key=std::pair<std::string,std::uint16_t>;
using Path=std::filesystem::path;
Key keyOf(const neoshared::ResourceDocument& source) {
    return {neomdl::resourceKey(neoshared::pathToUtf8(neoshared::pathFromUtf8(source.fileName).stem())),source.type};
}
neoshared::ResourceDocument looseResource(const Path& path) {
    const auto type=neoshared::bif::resourceTypeFromExtension(neoshared::pathToUtf8(path.extension()),neoshared::bif::GameProfile::KotOR).value();
    return {neomdl::resourceFileIdentity(path),neoshared::pathToUtf8(path.filename()),neoshared::pathToUtf8(path),type,neomdl::readResourceFile(path),{path}};
}
std::vector<std::string> scriptLiterals(const std::vector<std::uint8_t>& code) {
    // NCS V1.0: 13-byte header, then opcode/auxiliary-byte instructions.
    // Lengths and CONST-string layout follow nwvmachineinternal.cpp.
    constexpr std::array<std::size_t,44> lengths{{8,2,8,6,5,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,6,2,6,6,6,2,8,2,6,6,6,8,8,6,6,2,2,10}};
    std::vector<std::string> strings;
    for(std::size_t offset=13;offset<code.size();) {
        const auto opcode=code.at(offset),type=code.at(offset+1);auto length=lengths.at(std::size_t(opcode)-1);
        if(opcode==4&&type==5) {
            const auto size=(std::size_t(code.at(offset+2))<<8u)|code.at(offset+3);length=4+size;
            std::string value;for(std::size_t i=0;i<size;++i)value.push_back(char(code.at(offset+4+i)));
            value.erase(std::find(value.begin(),value.end(),'\0'),value.end());
            strings.push_back(std::move(value));
        } else if((opcode==11||opcode==12)&&type==36)length=4;
        offset+=length;
    }
    return strings;
}
}

std::optional<ModulePackageSources> chooseModulePackageSources(wxWindow* parent,std::vector<Path> archives,
    const Path& looseDirectory,bool dark) {
    wxDialog dialog(parent,wxID_ANY,"Package module",wxDefaultPosition,parent->FromDIP(wxSize(760,560)),wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
    auto* box=new wxBoxSizer(wxVERTICAL);
    auto* note=new wxStaticText(&dialog,wxID_ANY,"Combine RIM, MOD and dialogue ERF archives. Top archive wins duplicate names. Current editor resources and imported templates override the selected archives.");
    note->Wrap(parent->FromDIP(700));box->Add(note,0,wxEXPAND|wxALL,10);
    auto* list=new wxListBox(&dialog,wxID_ANY);box->Add(list,1,wxEXPAND|wxLEFT|wxRIGHT,10);
    std::vector<wxButton*> reorder;
    const auto refresh=[&] {list->Clear();for(const auto& path:archives)list->Append(neosettings::pathToWx(path));for(auto* button:reorder)button->Disable();};refresh();
    auto* buttons=new wxWrapSizer(wxHORIZONTAL);
    auto* add=new wxButton(&dialog,wxID_ANY,"Add archives...");buttons->Add(add,0,wxALL,4);
    add->Bind(wxEVT_BUTTON,[&](wxCommandEvent&){
        wxFileDialog picker(&dialog,"Add module archives",{},{},"Module archives|*.rim;*.RIM;*.mod;*.MOD;*.erf;*.ERF",wxFD_OPEN|wxFD_FILE_MUST_EXIST|wxFD_MULTIPLE);
        if(picker.ShowModal()!=wxID_OK)return;
        wxArrayString paths;picker.GetPaths(paths);
        for(const auto& value:paths) {const auto path=neosettings::pathFromWx(value);if(std::find(archives.begin(),archives.end(),path)==archives.end())archives.push_back(path);}
        refresh();
    });
    for(int i=0;i<3;++i) {
        auto* button=new wxButton(&dialog,wxID_ANY,i==0?"Remove":i==1?"Move up":"Move down");buttons->Add(button,0,wxALL,4);reorder.push_back(button);button->Disable();
        button->Bind(wxEVT_BUTTON,[&,i](wxCommandEvent&){
            const auto selected=list->GetSelection();
            if(i==0)archives.erase(archives.begin()+selected);
            else std::swap(archives.at(selected),archives.at(selected+(i==1?-1:1)));
            refresh();if(i!=0)list->SetSelection(selected+(i==1?-1:1));
            reorder[0]->Enable(list->GetSelection()!=wxNOT_FOUND);reorder[1]->Enable(list->GetSelection()>0);
            reorder[2]->Enable(list->GetSelection()!=wxNOT_FOUND&&list->GetSelection()+1<int(list->GetCount()));
        });
    }
    list->Bind(wxEVT_LISTBOX,[&](wxCommandEvent&){
        const auto index=list->GetSelection();reorder[0]->Enable(index!=wxNOT_FOUND);reorder[1]->Enable(index>0);reorder[2]->Enable(index!=wxNOT_FOUND&&index+1<int(list->GetCount()));
    });
    box->Add(buttons,0,wxEXPAND|wxALL,6);
    if(!looseDirectory.empty())box->Add(new wxStaticText(&dialog,wxID_ANY,"Loose module folder: "+neosettings::pathToWx(looseDirectory)),0,wxEXPAND|wxALL,10);
    std::vector<Path> extras;auto* extraList=new wxListBox(&dialog,wxID_ANY);extraList->SetMinSize(parent->FromDIP(wxSize(-1,70)));
    box->Add(extraList,0,wxEXPAND|wxLEFT|wxRIGHT,10);auto* extraButtons=new wxBoxSizer(wxHORIZONTAL);
    auto* extra=new wxButton(&dialog,wxID_ANY,"Add extra resources...");extraButtons->Add(extra,0,wxALL,4);
    extra->Bind(wxEVT_BUTTON,[&](wxCommandEvent&){
        wxFileDialog picker(&dialog,"Add explicit resources (including runtime script dependencies)",{},{},"Game resources|*.*",wxFD_OPEN|wxFD_FILE_MUST_EXIST|wxFD_MULTIPLE);
        if(picker.ShowModal()!=wxID_OK)return;
        wxArrayString paths;picker.GetPaths(paths);
        for(const auto& value:paths) {const auto path=neosettings::pathFromWx(value);extras.push_back(path);extraList->Append(value);}
    });
    auto* remove=new wxButton(&dialog,wxID_ANY,"Remove extra");remove->Disable();extraButtons->Add(remove,0,wxALL,4);
    extraList->Bind(wxEVT_LISTBOX,[&](wxCommandEvent&){remove->Enable(extraList->GetSelection()!=wxNOT_FOUND);});
    remove->Bind(wxEVT_BUTTON,[&](wxCommandEvent&){const auto i=extraList->GetSelection();extras.erase(extras.begin()+i);extraList->Delete(i);remove->Disable();});
    box->Add(extraButtons,0,wxLEFT|wxRIGHT,6);
    auto* collect=new wxCheckBox(&dialog,wxID_ANY,"Collect referenced blueprints, compiled scripts and dialogues");collect->SetValue(true);box->Add(collect,0,wxALL,10);
    auto* limits=new wxStaticText(&dialog,wxID_ANY,"Collection follows native GFF references and matching names in NCS string constants. Runtime-constructed names and custom models, textures or audio need explicit inputs. Existing package resources win dependency name collisions. TLK entries remain in the game's talk table.");
    limits->Wrap(parent->FromDIP(700));box->Add(limits,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,10);
    box->Add(dialog.CreateButtonSizer(wxOK|wxCANCEL),0,wxALIGN_RIGHT|wxALL,10);dialog.SetSizer(box);wxui::applyTheme(&dialog,dark);
    if(dialog.ShowModal()!=wxID_OK)return std::nullopt;
    return ModulePackageSources{std::move(archives),std::move(extras),collect->GetValue()};
}

ModulePackage collectModulePackage(const ModulePackageSources& sources,const Path& looseDirectory,
    std::vector<ModulePackageInput> edits,const std::vector<ModulePackageInput>& inlineSources,const neomdl::ResourceResolver& resolver,neomdl::model::GameVersion game,
    const std::function<void(const std::string&)>& progress) {
    neomdl::ResourceLoadScope loading(neomdl::resourceLoadJob());
    std::map<Key,ModulePackageInput> entries;std::vector<std::string> shadowed;
    std::set<Key> edited;for(const auto& edit:edits)edited.insert(keyOf(edit.source));
    std::map<Key,Path> looseFiles;
    if(!looseDirectory.empty())for(const auto& file:std::filesystem::directory_iterator(looseDirectory)) {
        const auto type=neoshared::bif::resourceTypeFromExtension(neoshared::pathToUtf8(file.path().extension()),neoshared::bif::GameProfile::KotOR);
        if(!type)continue;
        const Key key{neomdl::resourceKey(neoshared::pathToUtf8(file.path().stem())),*type};
        if(!edited.count(key)&&file.is_regular_file())looseFiles.insert_or_assign(key,file.path());
    }
    for(const auto& path:sources.extraFiles) {
        const auto type=neoshared::bif::resourceTypeFromExtension(neoshared::pathToUtf8(path.extension()),neoshared::bif::GameProfile::KotOR).value();
        const Key key{neomdl::resourceKey(neoshared::pathToUtf8(path.stem())),type};
        if(!edited.count(key))looseFiles.insert_or_assign(key,path);
    }
    for(const auto& path:sources.archives) {
        progress("Reading "+neoshared::pathToUtf8(path.filename()));neoshared::erf::ErfArchive archive(path);
        for(std::size_t i=0;i<archive.count();++i) {
            const auto& resource=archive.resource(i);const Key key{neomdl::resourceKey(resource.resref),resource.restype};
            const auto filename=key.first+"."+neomdl::resourceExtension(key.second);
            if(edited.count(key)||looseFiles.count(key)||entries.count(key)) {shadowed.push_back(filename+" from "+neoshared::pathToUtf8(path));continue;}
            neoshared::ResourceDocument document{neoshared::pathToUtf8(path)+"#"+filename,filename,neoshared::pathToUtf8(path),key.second,archive.read_resource(i),{path}};
            entries.emplace(key,ModulePackageInput{std::move(document),resolver});
        }
    }
    for(const auto& [key,path]:looseFiles) {
        auto source=looseResource(path);entries.insert_or_assign(key,ModulePackageInput{std::move(source),resolver});
    }
    for(auto& edit:edits) {const auto key=keyOf(edit.source);entries.insert_or_assign(key,std::move(edit));}
    const auto originalCount=entries.size();std::vector<Key> queue;for(const auto& [key,entry]:entries)queue.push_back(key);
    std::set<std::string> missing;std::vector<std::string> dependencies;
    const auto add=[&](const std::string& value,std::uint16_t type,const neomdl::ResourceResolver& resolve,bool required) {
        const Key key{neomdl::resourceKey(value.substr(0,std::min<std::size_t>(value.find('\0'),16))),type};
        if(key.first.empty()||key.first=="****"||entries.count(key))return;
        const auto filename=key.first+"."+neomdl::resourceExtension(type);
        if(auto source=resolve(key.first,key.second)) {
            missing.erase(filename);dependencies.push_back(filename);queue.push_back(key);
            entries.emplace(key,ModulePackageInput{std::move(*source),resolve});
        } else if(required)missing.insert(filename);
    };
    constexpr std::array<std::uint16_t,13> gffTypes{{2012,2014,2023,2025,2027,2029,2032,2035,2040,2042,2044,2051,2058}};
    for(std::size_t n=0;sources.collectDependencies&&n<inlineSources.size()+queue.size();++n) {
        const auto& entry=n<inlineSources.size()?inlineSources[n]:entries.at(queue[n-inlineSources.size()]);
        const auto key=keyOf(entry.source);const auto& resolve=entry.dependencies;
        progress("Collecting dependencies: "+entry.source.fileName);
        if(key.second==2010) {
            for(const auto& name:scriptLiterals(entry.source.bytes))
                for(const auto type:{2010,2029,2025,2027,2032,2035,2040,2042,2044,2051})add(name,std::uint16_t(type),resolve,false);
        } else if(std::find(gffTypes.begin(),gffTypes.end(),key.second)!=gffTypes.end()) {
            neogff::GffFile file;file.LoadBytes(entry.source.bytes);
            const auto* flag=static_cast<const neogff::GffByteField*>(file.root()->GetFieldByLabel("UseTemplates"));
            const bool templateGit=key.second==2023&&flag&&flag->value!=0;
            std::function<void(const neogff::GffStruct&,const std::string&)> walk;
            walk=[&](const neogff::GffStruct& record,const std::string& context) {
                for(const auto& field:record.allFields()) {
                    if(const auto* list=dynamic_cast<const neogff::GffList*>(field.get())) {
                        for(const auto& child:list->allStructs())walk(*child,list->GetLabel());
                    } else if(const auto* child=dynamic_cast<const neogff::GffStruct*>(field.get()))walk(*child,context);
                    else if(field->fieldtype==neogff::FIELD_TYPE_RESREF) {
                        const auto label=field->GetLabel(),name=field->GetString();
                        if(!templateGit&&label=="Conversation")add(name,2029,resolve,true);
                        else if(!templateGit&&(key.second!=2029||game==neomdl::model::GameVersion::Kotor2||(label!="Script2"&&label!="Active2"))&&
                            (label.find("Script")==0||label.find("Mod_On")==0||label.find("On")==0||
                            (key.second==2029&&(label=="Active"||label=="Active2"||label=="EndConversation"||label=="EndConverAbort"))))add(name,2010,resolve,true);
                        else if(!templateGit&&(label=="EquippedRes"||label=="InventoryRes"))add(name,2025,resolve,true);
                        else if(!templateGit&&((context=="CreatureList"&&label=="ResRef")||label=="SpawnResRef"))add(name,2027,resolve,true);
                        else if(templateGit&&context=="StoreList"&&label=="ResRef")add(name,2051,resolve,true);
                        else if(templateGit&&label=="TemplateResRef") {
                            const std::map<std::string,std::uint16_t> types={{"Creature List",2027},{"Placeable List",2044},{"Door List",2042},{"TriggerList",2032},{"SoundList",2035},{"Encounter List",2040},{"List",2025}};
                            if(const auto type=types.find(context);type!=types.end())add(name,type->second,resolve,true);
                        } else if(key.second==2014&&(label=="Area_Name"||label=="Mod_Entry_Area")) {
                            add(name,2012,resolve,true);add(name,2023,resolve,true);add(name,3000,resolve,false);add(name,3001,resolve,false);
                        }
                    }
                }
            };walk(*file.root(),{});
        }
    }
    ModulePackage result;std::size_t scripts=0,dialogues=0;
    for(auto& [key,entry]:entries) {scripts+=key.second==2010;dialogues+=key.second==2029;result.resources.emplace(key,std::move(entry.source));}
    std::ostringstream report;report<<result.resources.size()<<" resources ready for MOD export\n"<<sources.archives.size()<<" input archives; "<<originalCount<<" merged/editor resources; "<<dependencies.size()<<" collected dependencies\n"<<scripts<<" compiled scripts; "<<dialogues<<" dialogues\n\n";
    report<<"Dependency rules: "<<(game==neomdl::model::GameVersion::Kotor1?"K1":"K2")<<".\n";
    report<<"Dependency collection: "<<(sources.collectDependencies?"enabled":"disabled")<<". Runtime-constructed names and custom models, textures or audio require explicit inputs. TLK entries remain in the game's talk table.\n";
    report<<"\nUnresolved native references ("<<missing.size()<<"):\n";for(const auto& name:missing)report<<name<<'\n';
    report<<"\nCollected dependencies:\n";for(const auto& name:dependencies)report<<name<<'\n';
    report<<"\nLower-priority archive entries ("<<shadowed.size()<<"):\n";for(const auto& name:shadowed)report<<name<<'\n';
    result.report=report.str();return result;
}

}
