#include "xkit/widget.h"
#include "xkit/application.h"
#include "xkit/xbus_client.h"
#include "shared/protocol.h"
#include <memory>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <functional>

using namespace fbx::xkit;
using namespace fbx::shared;

// ─── App entry ────────────────────────────────────────────────────────────────

struct AppEntry {
    std::string name;
    std::string identifier;
    std::string version;
};

// ─── Paginated ListApps ───────────────────────────────────────────────────────

static void list_apps_all(XBusClient* xbus, uint32_t page,
                           std::shared_ptr<std::vector<AppEntry>> out,
                           std::function<void(std::vector<AppEntry>)> done) {
    uint32_t page_req = page;
    xbus->Call("com.freebsdx.AppManager",
               static_cast<uint32_t>(AppManagerOpcode::ListApps),
               {reinterpret_cast<const uint8_t*>(&page_req), sizeof(page_req)},
               [xbus, out, done = std::move(done)](const XBusMessage& resp) mutable {
                   AppList list{};
                   std::memcpy(&list, resp.payload_, sizeof(list));

                   if (list.count_ == 1) {
                       AppEntry e;
                       e.name       = list.app_.name_;
                       e.identifier = list.app_.identifier_;
                       e.version    = list.app_.version_;
                       out->push_back(std::move(e));
                   }

                   if (list.has_more_) {
                       list_apps_all(xbus, list.page_ + 1, out, std::move(done));
                   } else {
                       done(std::move(*out));
                   }
               });
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    // ── Mutable UI state ──────────────────────────────────────────────────
    auto status_label = std::make_shared<TextWidget>("Ready.", 13.0f);
    status_label->setColor({0.55f, 0.55f, 0.6f, 1.0f});

    auto progress_bar = std::make_shared<ProgressBarWidget>(0.0f);

    auto app_list_col = std::make_shared<FlexColumn>(
        std::vector<std::shared_ptr<Widget>>{
            std::make_shared<TextWidget>("Loading…", 14.0f)});

    auto path_input = std::make_shared<TextInputWidget>("/path/to/App.xpkg");

    // ── Rebuild the installed-apps column (called after list_apps_all) ────
    // Use shared_ptr-to-function so the lambda can capture itself.
    auto rebuild = std::make_shared<std::function<void()>>();

    *rebuild = [app_list_col, status_label, rebuild]() {
        auto* xbus = XApplication::getBus();
        if (!xbus) return;

        status_label->setText("Refreshing…");
        app_list_col->clearChildren();

        auto out = std::make_shared<std::vector<AppEntry>>();
        list_apps_all(xbus, 0, out,
            [app_list_col, status_label, rebuild](std::vector<AppEntry> apps) {
                app_list_col->clearChildren();

                if (apps.empty()) {
                    auto empty = std::make_shared<TextWidget>("No apps installed.", 14.0f);
                    empty->setColor({0.6f, 0.6f, 0.65f, 1.0f});
                    app_list_col->addChild(empty);
                    status_label->setText("0 apps installed.");
                    return;
                }

                for (size_t i = 0; i < apps.size(); ++i) {
                    std::string captured_id   = apps[i].identifier;
                    std::string captured_name = apps[i].name;

                    auto name_lbl = std::make_shared<TextWidget>(apps[i].name, 15.0f);
                    auto ver_lbl  = std::make_shared<TextWidget>(
                        "v" + apps[i].version + "  •  " + apps[i].identifier, 12.0f);
                    ver_lbl->setColor({0.55f, 0.55f, 0.6f, 1.0f});

                    auto info_col = std::make_shared<FlexColumn>(
                        std::vector<std::shared_ptr<Widget>>{name_lbl, ver_lbl});

                    auto remove_btn = std::make_shared<ButtonWidget>(
                        std::make_shared<TextWidget>("Remove", 13.0f),
                        [captured_id, captured_name, status_label, rebuild]() {
                            auto* xbus = XApplication::getBus();
                            if (!xbus) return;
                            status_label->setText("Removing " + captured_name + "…");
                            AppIdentifierRequest req{};
                            std::strncpy(req.identifier_, captured_id.c_str(),
                                         sizeof(req.identifier_) - 1);
                            xbus->Call("com.freebsdx.AppManager",
                                       static_cast<uint32_t>(AppManagerOpcode::Uninstall),
                                       {reinterpret_cast<const uint8_t*>(&req), sizeof(req)},
                                       [](const XBusMessage&) {
                                           // Progress arrives via broadcast
                                       });
                        });

                    auto tile = std::make_shared<ListTileWidget>();
                    tile->setTitle(info_col).setTrailing(remove_btn);
                    app_list_col->addChild(tile);

                    if (i + 1 < apps.size())
                        app_list_col->addChild(std::make_shared<DividerWidget>(0.5f));
                }

                status_label->setText(std::to_string(apps.size()) + " app(s) installed.");
            });
    };

    // ── Root layout ───────────────────────────────────────────────────────
    auto title = std::make_shared<TextWidget>("App Manager", 28.0f);
    title->setPadding(8);

    // ── Install section ───────────────────────────────────────────────────
    auto install_btn = std::make_shared<ButtonWidget>(
        std::make_shared<TextWidget>("Install", 14.0f),
        [path_input, status_label]() {
            auto* xbus = XApplication::getBus();
            if (!xbus) return;
            std::string path = path_input->text();
            if (path.empty() || path == "/path/to/App.xpkg") {
                status_label->setText("Enter a .xpkg or .xapp path first.");
                return;
            }
            status_label->setText("Requesting install…");
            AppInstallRequest req{};
            std::strncpy(req.path_, path.c_str(), sizeof(req.path_) - 1);
            xbus->Call("com.freebsdx.AppManager",
                       static_cast<uint32_t>(AppManagerOpcode::Install),
                       {reinterpret_cast<const uint8_t*>(&req), sizeof(req)},
                       [](const XBusMessage&) {
                           // ACK; real progress comes from broadcast
                       });
        });

    auto input_row = std::make_shared<FlexRow>(
        std::vector<std::shared_ptr<Widget>>{path_input, install_btn});
    input_row->setPadding(4);

    auto install_header = std::make_shared<TextWidget>("INSTALL", 13.0f);
    install_header->setColor({0.55f, 0.55f, 0.6f, 1.0f});
    install_header->setPadding(4);

    auto install_tile = std::make_shared<ListTileWidget>();
    install_tile
        ->setTitle(std::make_shared<TextWidget>("Install from path", 15.0f))
        .setSubtitle(input_row);

    auto s_install = std::make_shared<CardWidget>(
        std::make_shared<FlexColumn>(
            std::vector<std::shared_ptr<Widget>>{install_header, install_tile}));

    // ── Progress section ──────────────────────────────────────────────────
    auto progress_col = std::make_shared<FlexColumn>(
        std::vector<std::shared_ptr<Widget>>{status_label, progress_bar});
    progress_col->setPadding(4);
    auto s_status = std::make_shared<CardWidget>(progress_col);

    // ── Installed apps section ────────────────────────────────────────────
    auto refresh_btn = std::make_shared<ButtonWidget>(
        std::make_shared<TextWidget>("Refresh", 13.0f),
        [rebuild]() { if (*rebuild) (*rebuild)(); });

    auto installed_header = std::make_shared<TextWidget>("INSTALLED APPS", 13.0f);
    installed_header->setColor({0.55f, 0.55f, 0.6f, 1.0f});
    installed_header->setPadding(4);

    auto header_row = std::make_shared<FlexRow>(
        std::vector<std::shared_ptr<Widget>>{installed_header, refresh_btn});

    auto s_apps = std::make_shared<CardWidget>(app_list_col);

    // ── Discover / Remote Catalog section ────────────────────────────────────
    auto discover_status = std::make_shared<TextWidget>("Run 'xpkg update' or click Refresh Catalog.", 12.0f);
    discover_status->setColor({0.55f, 0.55f, 0.6f, 1.0f});

    auto remote_list_col = std::make_shared<FlexColumn>(std::vector<std::shared_ptr<Widget>>{});

    auto load_remote_page = std::make_shared<std::function<void(uint32_t)>>();
    *load_remote_page = [remote_list_col, discover_status, load_remote_page](uint32_t page) {
        auto* xbus = XApplication::getBus();
        if (!xbus) return;

        AppManagerPageRequest req{};
        req.page_ = page;
        xbus->Call("com.freebsdx.AppManager",
            static_cast<uint32_t>(AppManagerCatalogOpcode::ListRemote),
            {reinterpret_cast<const uint8_t*>(&req), sizeof(req)},
            [remote_list_col, discover_status, load_remote_page, page](const XBusMessage& resp) {
                AppManagerRemotePage pg{};
                std::memcpy(&pg, resp.payload_, sizeof(pg));

                if (page == 0) {
                    remote_list_col->clearChildren();
                    if (!pg.valid_) {
                        discover_status->setText("No packages in catalog. Run 'xpkg update'.");
                        return;
                    }
                    discover_status->setText(std::format("{} package(s) available.", pg.total_));
                }

                if (pg.valid_) {
                    std::string name    = pg.name_;
                    std::string ver     = pg.version_;
                    std::string cat     = pg.category_;
                    std::string desc    = pg.description_;
                    std::string id      = pg.identifier_;
                    uint32_t    size_kb = pg.size_kb_;
                    bool        inst    = pg.installed_ != 0;

                    auto name_w = std::make_shared<TextWidget>(name, 15.0f);
                    auto meta_w = std::make_shared<TextWidget>(
                        "v" + ver + "  \u2022  " + cat + (size_kb ? "  \u2022  " + std::to_string(size_kb) + " KB" : ""), 11.0f);
                    meta_w->setColor({0.5f, 0.5f, 0.6f, 1.0f});
                    auto desc_w = std::make_shared<TextWidget>(desc, 12.0f);
                    desc_w->setColor({0.72f, 0.72f, 0.72f, 1.0f});

                    auto info_col = std::make_shared<FlexColumn>(std::vector<std::shared_ptr<Widget>>{
                        name_w, meta_w, std::make_shared<Spacer>(1,2), desc_w
                    });

                    std::shared_ptr<Widget> trailing;
                    if (inst) {
                        auto installed_lbl = std::make_shared<TextWidget>("\u2713 Installed", 12.0f);
                        installed_lbl->setColor({0.4f, 0.8f, 0.4f, 1.0f});
                        trailing = installed_lbl;
                    } else {
                        trailing = std::make_shared<ButtonWidget>(
                            std::make_shared<TextWidget>("Install", 13.0f),
                            [id]() {
                                auto* xbus = XApplication::getBus();
                                if (!xbus) return;
                                AppManagerRemoteInstallRequest req{};
                                std::strncpy(req.identifier_, id.c_str(), sizeof(req.identifier_)-1);
                                xbus->Call("com.freebsdx.AppManager",
                                    static_cast<uint32_t>(AppManagerCatalogOpcode::InstallRemote),
                                    {reinterpret_cast<const uint8_t*>(&req), sizeof(req)},
                                    [](const XBusMessage&){});
                            });
                    }

                    auto tile = std::make_shared<ListTileWidget>();
                    tile->setTitle(info_col).setTrailing(trailing);
                    remote_list_col->addChild(tile);
                    remote_list_col->addChild(std::make_shared<DividerWidget>(0.3f));

                    if (pg.has_more_) {
                        auto next_weak = std::weak_ptr<std::function<void(uint32_t)>>(load_remote_page);
                        if (auto fn = next_weak.lock()) (*fn)(page + 1);
                    }
                }
            });
    };

    // Search remote
    auto remote_search_input = std::make_shared<TextInputWidget>("Search packages...");
    remote_search_input->setOnChanged([remote_list_col, discover_status](const std::string& q) {
        auto* xbus = XApplication::getBus();
        if (!xbus || q.size() < 2) return;
        AppManagerSearchRequest req{};
        std::strncpy(req.query_, q.c_str(), sizeof(req.query_)-1);
        req.page_ = 0;
        xbus->Call("com.freebsdx.AppManager",
            static_cast<uint32_t>(AppManagerCatalogOpcode::SearchRemote),
            {reinterpret_cast<const uint8_t*>(&req), sizeof(req)},
            [remote_list_col, discover_status, q](const XBusMessage& resp) {
                AppManagerRemotePage pg{};
                std::memcpy(&pg, resp.payload_, sizeof(pg));
                remote_list_col->clearChildren();
                if (!pg.valid_) {
                    discover_status->setText("No results for \"" + q + "\"");
                    return;
                }
                discover_status->setText(std::format("{} result(s) for \"{}\"", pg.total_, q));
                if (pg.valid_) {
                    auto name_w = std::make_shared<TextWidget>(std::string(pg.name_), 15.0f);
                    auto meta_w = std::make_shared<TextWidget>(
                        "v" + std::string(pg.version_) + "  \u2022  " + std::string(pg.category_), 11.0f);
                    meta_w->setColor({0.5f, 0.5f, 0.6f, 1.0f});
                    auto tile = std::make_shared<ListTileWidget>();
                    tile->setTitle(std::make_shared<FlexColumn>(std::vector<std::shared_ptr<Widget>>{name_w, meta_w}));
                    remote_list_col->addChild(tile);
                }
            });
    });

    // Refresh Catalog button
    auto refresh_catalog_btn = std::make_shared<ButtonWidget>(
        std::make_shared<TextWidget>("Refresh Catalog", 13.0f),
        [discover_status, load_remote_page]() {
            auto* xbus = XApplication::getBus();
            if (!xbus) return;
            discover_status->setText("Downloading catalog...");
            xbus->Call("com.freebsdx.AppManager",
                static_cast<uint32_t>(AppManagerCatalogOpcode::UpdateCatalog),
                {}, [load_remote_page](const XBusMessage&) {
                    // CatalogUpdated broadcast will arrive separately
                });
        });

    auto discover_header_row = std::make_shared<FlexRow>(std::vector<std::shared_ptr<Widget>>{
        std::make_shared<TextWidget>("DISCOVER", 13.0f),
        std::make_shared<Spacer>(1, 1, true),
        refresh_catalog_btn
    });

    auto s_discover = std::make_shared<CardWidget>(std::make_shared<FlexColumn>(
        std::vector<std::shared_ptr<Widget>>{
            discover_status,
            std::make_shared<Spacer>(1, 8),
            remote_search_input,
            std::make_shared<Spacer>(1, 8),
            std::make_shared<ScrollViewWidget>(remote_list_col)
        }));

    // ── Package Signing section ───────────────────────────────────────────────

    auto signing_status = std::make_shared<TextWidget>("Loading signing policy…", 12.0f);
    signing_status->setColor({0.55f, 0.55f, 0.6f, 1.0f});

    auto keys_col = std::make_shared<FlexColumn>(std::vector<std::shared_ptr<Widget>>{});

    auto require_switch = std::make_shared<SwitchWidget>(false);

    // Paginated key list loader
    auto load_keys_page = std::make_shared<std::function<void(uint32_t)>>();
    *load_keys_page = [keys_col, signing_status, load_keys_page](uint32_t page) {
        auto* xbus = XApplication::getBus();
        if (!xbus) return;

        PkgKeyPageRequest req{};
        req.page_ = page;
        xbus->Call("com.freebsdx.PackageSigning",
            static_cast<uint32_t>(PackageSigningOpcode::ListKeys),
            {reinterpret_cast<const uint8_t*>(&req), sizeof(req)},
            [keys_col, signing_status, load_keys_page, page](const XBusMessage& resp) {
                PkgKeyListPage lp{};
                std::memcpy(&lp, resp.payload_, sizeof(lp));

                if (page == 0) {
                    keys_col->clearChildren();
                    if (lp.total_ == 0) {
                        auto empty = std::make_shared<TextWidget>(
                            "No trusted keys. Add keys with 'xpkg trust <pubkey>'.", 12.0f);
                        empty->setColor({0.6f, 0.6f, 0.65f, 1.0f});
                        keys_col->addChild(empty);
                        signing_status->setText("0 trusted keys");
                        return;
                    }
                    signing_status->setText(std::format("{} trusted key(s)", lp.total_));
                }

                for (uint32_t i = 0; i < lp.count_; ++i) {
                    const PkgKeyEntry& ke = lp.entries_[i];
                    std::string fp   = ke.fingerprint_;
                    std::string name = ke.name_;
                    std::string type = ke.type_;

                    std::string type_label = (type == "ms") ? "[minisign]" : "[GPG]";
                    auto name_w = std::make_shared<TextWidget>(
                        name.empty() ? fp : name, 14.0f);
                    auto meta_w = std::make_shared<TextWidget>(
                        type_label + "  " + fp, 11.0f);
                    meta_w->setColor({0.5f, 0.5f, 0.6f, 1.0f});

                    auto info_col = std::make_shared<FlexColumn>(
                        std::vector<std::shared_ptr<Widget>>{name_w, meta_w});

                    // Remove button
                    auto rm_btn = std::make_shared<ButtonWidget>(
                        std::make_shared<TextWidget>("Remove", 12.0f),
                        [fp, load_keys_page]() {
                            auto* xbus = XApplication::getBus();
                            if (!xbus) return;
                            PkgKeyIdRequest req{};
                            std::strncpy(req.fingerprint_, fp.c_str(),
                                         sizeof(req.fingerprint_) - 1);
                            xbus->Call("com.freebsdx.PackageSigning",
                                static_cast<uint32_t>(PackageSigningOpcode::RemoveKey),
                                {reinterpret_cast<const uint8_t*>(&req), sizeof(req)},
                                [load_keys_page](const XBusMessage&) {
                                    if (*load_keys_page) (*load_keys_page)(0);
                                });
                        });

                    auto tile = std::make_shared<ListTileWidget>();
                    tile->setTitle(info_col).setTrailing(rm_btn);
                    keys_col->addChild(tile);
                    keys_col->addChild(std::make_shared<DividerWidget>(0.3f));
                }

                if (lp.has_more_) {
                    auto weak = std::weak_ptr<std::function<void(uint32_t)>>(load_keys_page);
                    if (auto fn = weak.lock()) (*fn)(page + 1);
                }
            });
    };

    // Import key by path input
    auto key_path_input = std::make_shared<TextInputWidget>("/path/to/key.pub");
    auto import_key_btn = std::make_shared<ButtonWidget>(
        std::make_shared<TextWidget>("Import Key", 13.0f),
        [key_path_input, signing_status, load_keys_page]() {
            auto* xbus = XApplication::getBus();
            if (!xbus) return;
            std::string path = key_path_input->text();
            if (path.empty() || path == "/path/to/key.pub") {
                signing_status->setText("Enter a .pub or .asc key file path first.");
                return;
            }
            PkgKeyImportRequest req{};
            std::strncpy(req.path_, path.c_str(), sizeof(req.path_) - 1);
            xbus->Call("com.freebsdx.PackageSigning",
                static_cast<uint32_t>(PackageSigningOpcode::ImportKey),
                {reinterpret_cast<const uint8_t*>(&req), sizeof(req)},
                [signing_status, load_keys_page](const XBusMessage& resp) {
                    PkgSigningStatusResponse sr{};
                    std::memcpy(&sr, resp.payload_, sizeof(sr));
                    signing_status->setText(std::string(sr.message_));
                    if (sr.success_ && *load_keys_page) (*load_keys_page)(0);
                });
        });

    auto key_import_row = std::make_shared<FlexRow>(
        std::vector<std::shared_ptr<Widget>>{key_path_input, import_key_btn});
    key_import_row->setPadding(4);

    // Policy switch row
    require_switch->setOnChanged([signing_status](bool on) {
        auto* xbus = XApplication::getBus();
        if (!xbus) return;
        PkgSigningPolicy policy{};
        policy.require_signed_ = on ? 1u : 0u;
        policy.allow_unknown_  = on ? 0u : 1u;
        xbus->Call("com.freebsdx.PackageSigning",
            static_cast<uint32_t>(PackageSigningOpcode::SetPolicy),
            {reinterpret_cast<const uint8_t*>(&policy), sizeof(policy)},
            [signing_status, on](const XBusMessage&) {
                signing_status->setText(on
                    ? "Unsigned packages will be rejected."
                    : "Unsigned packages allowed (warning only).");
            });
    });

    auto switch_label = std::make_shared<TextWidget>("Require signed packages", 14.0f);
    auto policy_row = std::make_shared<FlexRow>(
        std::vector<std::shared_ptr<Widget>>{switch_label,
            std::make_shared<Spacer>(1, 1, true), require_switch});
    policy_row->setPadding(4);

    auto signing_header_row = std::make_shared<FlexRow>(std::vector<std::shared_ptr<Widget>>{
        std::make_shared<TextWidget>("PACKAGE SIGNING", 13.0f),
        std::make_shared<Spacer>(1, 1, true),
        std::make_shared<TextWidget>("xpkg sign/verify/trust", 11.0f)
    });

    auto s_signing = std::make_shared<CardWidget>(std::make_shared<FlexColumn>(
        std::vector<std::shared_ptr<Widget>>{
            signing_status,
            std::make_shared<Spacer>(1, 6),
            policy_row,
            std::make_shared<DividerWidget>(0.4f),
            std::make_shared<Spacer>(1, 4),
            std::make_shared<TextWidget>("TRUSTED KEYS", 11.0f),
            std::make_shared<Spacer>(1, 4),
            std::make_shared<ScrollViewWidget>(keys_col),
            std::make_shared<Spacer>(1, 8),
            key_import_row,
        }));

    // ── Root layout ───────────────────────────────────────────────────────────

    auto content = std::make_shared<FlexColumn>(std::vector<std::shared_ptr<Widget>>{
        title,
        std::make_shared<Spacer>(1, 12),
        s_install,
        std::make_shared<Spacer>(1, 8),
        s_status,
        std::make_shared<Spacer>(1, 8),
        header_row,
        std::make_shared<Spacer>(1, 4),
        s_apps,
        std::make_shared<Spacer>(1, 8),
        discover_header_row,
        std::make_shared<Spacer>(1, 4),
        s_discover,
        std::make_shared<Spacer>(1, 8),
        signing_header_row,
        std::make_shared<Spacer>(1, 4),
        s_signing,
        std::make_shared<Spacer>(1, 24),
    });
    content->setPadding(20);

    auto root = std::make_shared<ScrollViewWidget>(content);

    return XApplication::Run(root, "/tmp/freebsdx.sock", true, false,
        [rebuild, status_label, progress_bar, discover_status, load_remote_page,
         load_keys_page, require_switch, signing_status](XBusClient& xbus) {
            XApplication::setTitle("App Manager");

            // Initial load
            if (*rebuild) (*rebuild)();

            // Load catalog on startup
            if (*load_remote_page) (*load_remote_page)(0);

            // Load signing policy and key list on startup
            xbus.Call("com.freebsdx.PackageSigning",
                static_cast<uint32_t>(PackageSigningOpcode::GetPolicy),
                {}, [require_switch, signing_status](const XBusMessage& resp) {
                    PkgSigningPolicy policy{};
                    std::memcpy(&policy, resp.payload_, sizeof(policy));
                    require_switch->setValue(policy.require_signed_ != 0);
                    signing_status->setText(policy.require_signed_
                        ? "Unsigned packages will be rejected."
                        : "Unsigned packages allowed (warning only).");
                });
            if (*load_keys_page) (*load_keys_page)(0);

            // Broadcast handler
            xbus.OnBroadcast([rebuild, status_label, progress_bar, discover_status,
                               load_remote_page, load_keys_page, require_switch,
                               signing_status](const XBusMessage& msg) {

                if (std::strcmp(msg.service_name_, "com.freebsdx.PackageSigning") == 0) {
                    if (msg.opcode_ == static_cast<uint32_t>(PackageSigningOpcode::SigningChanged)) {
                        // Reload keys and policy
                        if (*load_keys_page) (*load_keys_page)(0);
                        auto* xbus2 = XApplication::getBus();
                        if (xbus2) {
                            xbus2->Call("com.freebsdx.PackageSigning",
                                static_cast<uint32_t>(PackageSigningOpcode::GetPolicy),
                                {}, [require_switch, signing_status](const XBusMessage& resp) {
                                    PkgSigningPolicy p{};
                                    std::memcpy(&p, resp.payload_, sizeof(p));
                                    require_switch->setValue(p.require_signed_ != 0);
                                    signing_status->setText(p.require_signed_
                                        ? "Unsigned packages will be rejected."
                                        : "Unsigned packages allowed (warning only).");
                                });
                        }
                    }
                    return;
                }

                if (std::strcmp(msg.service_name_, "com.freebsdx.AppManager") != 0) return;

                if (msg.opcode_ == static_cast<uint32_t>(AppManagerCatalogOpcode::CatalogUpdated)) {
                    AppManagerCatalogState st{};
                    std::memcpy(&st, msg.payload_, sizeof(st));
                    if (st.error_) {
                        discover_status->setText("Catalog update failed: " + std::string(st.error_msg_));
                    } else {
                        discover_status->setText(std::format("{} packages available.", st.total_packages_));
                        if (*load_remote_page) (*load_remote_page)(0);
                    }
                    return;
                }

                if (msg.opcode_ != static_cast<uint32_t>(AppManagerOpcode::InstallProgress)) return;

                AppInstallProgress prog{};
                std::memcpy(&prog, msg.payload_, sizeof(prog));

                status_label->setText(std::string(prog.message_));
                progress_bar->setValue(static_cast<float>(prog.percent_) / 100.0f);

                // On success (1) or error (2), refresh the list.
                if (prog.status_ == 1 || prog.status_ == 2) {
                    if (*rebuild) (*rebuild)();
                }
            });
        });
}
