export default function(global, globalThis, window, $app_exports$, $app_evaluate$) {
    var org_app_require = $app_require$;
    (function(global, globalThis, window, $app_exports$, $app_evaluate$) {
        var setTimeout = global.setTimeout;
        var setInterval = global.setInterval;
        var clearTimeout = global.clearTimeout;
        var clearInterval = global.clearInterval;
        var $app_require$1 = global.$app_require$ || org_app_require;
        var createPageHandler = function() {
            return (()=>{
                var __webpack_modules__ = {};
                var __webpack_module_cache__ = {};
                function __webpack_require__(moduleId) {
                    var cachedModule = __webpack_module_cache__[moduleId];
                    if (void 0 !== cachedModule) return cachedModule.exports;
                    var module = __webpack_module_cache__[moduleId] = {
                        exports: {}
                    };
                    __webpack_modules__[moduleId](module, module.exports, __webpack_require__);
                    return module.exports;
                }
                (()=>{
                    __webpack_require__.rv = ()=>"1.7.12";
                })();
                (()=>{
                    __webpack_require__.ruid = "bundler=rspack@1.7.12";
                })();
                var $app_style$ = [
                    [
                        [
                            [
                                0,
                                "demo-page"
                            ]
                        ],
                        {
                            flexDirection: "column",
                            justifyContent: "center",
                            alignItems: "center"
                        }
                    ],
                    [
                        [
                            [
                                0,
                                "title"
                            ]
                        ],
                        {
                            fontSize: "20px",
                            textAlign: "center"
                        }
                    ],
                    [
                        [
                            [
                                0,
                                "btn"
                            ]
                        ],
                        {
                            width: "200px",
                            height: "40px",
                            marginTop: "20px",
                            borderRadius: "5px",
                            backgroundColor: "#09ba07",
                            fontSize: "20px",
                            color: "#ffffff"
                        }
                    ]
                ];
                var $app_script$ = function __scriptModule__(module, exports, $app_require$1) {
                    "use strict";
                    Object.defineProperty(exports, "__esModule", {
                        value: true
                    });
                    exports.default = void 0;
                    var _system = _interopRequireDefault($app_require$1("@app-module/system.router"));
                    function _interopRequireDefault(e) {
                        return e && e.__esModule ? e : {
                            default: e
                        };
                    }
                    var _default = exports.default = {
                        private: {
                            title: "示例页面"
                        },
                        routeDetail () {
                            _system.default.push({
                                uri: "/pages/detail"
                            });
                        }
                    };
                    const moduleOwn = exports.default || module.exports;
                    const accessors = [
                        'public',
                        'protected',
                        'private'
                    ];
                    if (moduleOwn.data && accessors.some(function(acc) {
                        return moduleOwn[acc];
                    })) throw new Error('页面VM对象中的属性data不可与"' + accessors.join(',') + '"同时存在，请使用private替换data名称');
                    if (!moduleOwn.data) {
                        moduleOwn.data = {};
                        moduleOwn._descriptor = {};
                        accessors.forEach(function(acc) {
                            const accType = typeof moduleOwn[acc];
                            if ('object' === accType) {
                                moduleOwn.data = Object.assign(moduleOwn.data, moduleOwn[acc]);
                                for(const name in moduleOwn[acc])moduleOwn._descriptor[name] = {
                                    access: acc
                                };
                            } else if ('function' === accType) console.warn('页面VM对象中的属性' + acc + '的值不能是函数，请使用对象');
                        });
                    }
                };
                var $app_template$ = function(vm) {
                    const _vm_ = vm || this;
                    return aiot.__ce__("div", {
                        __vm__: _vm_,
                        __opts__: {
                            classList: [
                                "demo-page"
                            ]
                        }
                    }, [
                        aiot.__ce__("text", {
                            __vm__: _vm_,
                            __opts__: {
                                classList: [
                                    "title"
                                ],
                                value: function() {
                                    return _vm_.$t("a.b") + ",欢迎打开" + _vm_.title;
                                }
                            }
                        }, []),
                        aiot.__ce__("input", {
                            __vm__: _vm_,
                            __opts__: {
                                classList: [
                                    "btn"
                                ],
                                type: "button",
                                value: "跳转到详情页",
                                events: {
                                    click: function(evt) {
                                        return _vm_.routeDetail(evt);
                                    }
                                }
                            }
                        }, [])
                    ]);
                };
                $app_exports$['entry'] = function($app_exports$) {
                    $app_script$({}, $app_exports$, $app_require$1);
                    $app_exports$.default.template = $app_template$;
                    $app_exports$.default.style = $app_style$;
                };
            })();
        };
        return createPageHandler();
    })(global, globalThis, window, $app_exports$, $app_evaluate$);
}

//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJmaWxlIjoicGFnZXNcXGluZGV4XFxpbmRleC5qcyIsInNvdXJjZXMiOlsid2VicGFjazovL2dlbm1pbmktczEvd2VicGFjay9ydW50aW1lL3JzcGFja192ZXJzaW9uIiwid2VicGFjazovL2dlbm1pbmktczEvd2VicGFjay9ydW50aW1lL3JzcGFja191bmlxdWVfaWQiLCJ3ZWJwYWNrOi8vZ2VubWluaS1zMS9zcmMvcGFnZXMvaW5kZXgvaW5kZXgudXgiXSwic291cmNlc0NvbnRlbnQiOlsiX193ZWJwYWNrX3JlcXVpcmVfXy5ydiA9ICgpID0+IChcIjEuNy4xMlwiKSIsIl9fd2VicGFja19yZXF1aXJlX18ucnVpZCA9IFwiYnVuZGxlcj1yc3BhY2tAMS43LjEyXCI7IiwiPHRlbXBsYXRlPlxuICA8ZGl2IGNsYXNzPVwiZGVtby1wYWdlXCI+XG4gICAgPHRleHQgY2xhc3M9XCJ0aXRsZVwiPnt7ICR0KFwiYS5iXCIpIH19LOasoui/juaJk+W8gHt7IHRpdGxlIH19PC90ZXh0PlxuICAgIDwhLS0g54K55Ye76Lez6L2s6K+m5oOF6aG1IC0tPlxuICAgIDxpbnB1dCBjbGFzcz1cImJ0blwiIHR5cGU9XCJidXR0b25cIiB2YWx1ZT1cIui3s+i9rOWIsOivpuaDhemhtVwiIG9uY2xpY2s9XCJyb3V0ZURldGFpbFwiIC8+XG4gIDwvZGl2PlxuPC90ZW1wbGF0ZT5cblxuPHNjcmlwdD5cbmltcG9ydCByb3V0ZXIgZnJvbSBcIkBzeXN0ZW0ucm91dGVyXCJcblxuZXhwb3J0IGRlZmF1bHQge1xuICAvLyDpobXpnaLnuqfnu4Tku7bnmoTmlbDmja7mqKHlnovvvIzlvbHlk43kvKDlhaXmlbDmja7nmoTopobnm5bmnLrliLbvvJpwcml2YXRl5YaF5a6a5LmJ55qE5bGe5oCn5LiN5YWB6K646KKr6KaG55uWXG4gIHByaXZhdGU6IHtcbiAgICB0aXRsZTogXCLnpLrkvovpobXpnaJcIlxuICB9LFxuXG4gIHJvdXRlRGV0YWlsKCkge1xuICAgIC8vIOi3s+i9rOWIsOW6lOeUqOWGheeahOafkOS4qumhtemdou+8jHJvdXRlcueUqOazleivpuinge+8muaWh+ahoy0+5o6l5Y+jLT7pobXpnaLot6/nlLFcbiAgICByb3V0ZXIucHVzaCh7XG4gICAgICB1cmk6IFwiL3BhZ2VzL2RldGFpbFwiXG4gICAgfSlcbiAgfVxufVxuPC9zY3JpcHQ+XG5cbjxzdHlsZT5cbi5kZW1vLXBhZ2Uge1xuICBmbGV4LWRpcmVjdGlvbjogY29sdW1uO1xuICBqdXN0aWZ5LWNvbnRlbnQ6IGNlbnRlcjtcbiAgYWxpZ24taXRlbXM6IGNlbnRlcjtcbn1cblxuLnRpdGxlIHtcbiAgZm9udC1zaXplOiAyMHB4O1xuICB0ZXh0LWFsaWduOiBjZW50ZXI7XG59XG5cbi5idG4ge1xuICB3aWR0aDogMjAwcHg7XG4gIGhlaWdodDogNDBweDtcbiAgbWFyZ2luLXRvcDogMjBweDtcbiAgYm9yZGVyLXJhZGl1czogNXB4O1xuICBiYWNrZ3JvdW5kLWNvbG9yOiAjMDliYTA3O1xuICBmb250LXNpemU6IDIwcHg7XG4gIGNvbG9yOiAjZmZmZmZmO1xufVxuPC9zdHlsZT5cbiJdLCJuYW1lcyI6WyJfX3dlYnBhY2tfcmVxdWlyZV9fIiwiX3N5c3RlbSIsIl9pbnRlcm9wUmVxdWlyZURlZmF1bHQiLCIkYXBwX3JlcXVpcmUkIiwiZSIsIl9fZXNNb2R1bGUiLCJkZWZhdWx0IiwiX2RlZmF1bHQiLCJleHBvcnRzIiwicHJpdmF0ZSIsInRpdGxlIiwicm91dGVEZXRhaWwiLCJyb3V0ZXIiLCJwdXNoIiwidXJpIl0sIm1hcHBpbmdzIjoiOzs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7O29CQUFBQSxvQkFBb0IsRUFBRSxHQUFHLElBQU87OztvQkNBaENBLG9CQUFvQixJQUFJLEdBQUc7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7b0JDUzNCLElBQUFDLFVBQUFDLHVCQUFBQyxlQUFBO29CQUFtQyxTQUFBRCx1QkFBQUUsQ0FBQTt3QkFBQSxPQUFBQSxLQUFBQSxFQUFBQyxVQUFBLEdBQUFELElBQUE7NEJBQUFFLFNBQUFGO3dCQUFBO29CQUFBO29CQUFBLElBQUFHLFdBQUFDLFFBQUFGLE9BQUEsR0FFcEI7d0JBRWJHLFNBQVM7NEJBQ1BDLE9BQU87d0JBQ1Q7d0JBRUFDOzRCQUVFQyxRQUFBQSxPQUFNLENBQUNDLElBQUksQ0FBQztnQ0FDVkMsS0FBSzs0QkFDUDt3QkFDRjtvQkFDRiJ9