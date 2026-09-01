export default function(global, globalThis, window, $app_exports$, $app_evaluate$) {
    var org_app_require = $app_require$;
    (function(global, globalThis, window, $app_exports$, $app_evaluate$) {
        var setTimeout = global.setTimeout;
        var setInterval = global.setInterval;
        var clearTimeout = global.clearTimeout;
        var clearInterval = global.clearInterval;
        var $app_require$1 = global.$app_require$ || org_app_require;
        var createAppHandler = function() {
            return (()=>{
                var __webpack_modules__ = {
                    "./src/manifest.json" (module) {
                        "use strict";
                        module.exports = JSON.parse('{"package":"com.application.watch.demo","name":"genmini-s1","versionName":"1.0.0","versionCode":1,"minPlatformVersion":1000,"icon":"/common/logo.png","deviceTypeList":["watch"],"features":[{"name":"system.router"}],"config":{"logLevel":"log","designWidth":"device-width"},"router":{"entry":"pages/index","pages":{"pages/index":{"component":"index"},"pages/detail":{"component":"detail"}}}}');
                    }
                };
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
                    __webpack_require__.g = (()=>{
                        if ('object' == typeof globalThis) return globalThis;
                        try {
                            return this || new Function('return this')();
                        } catch (e) {
                            if ('object' == typeof window) return window;
                        }
                    })();
                })();
                (()=>{
                    __webpack_require__.rv = ()=>"1.7.12";
                })();
                (()=>{
                    __webpack_require__.ruid = "bundler=rspack@1.7.12";
                })();
                (()=>{
                    var $app_style$ = [];
                    var $app_script$ = function __scriptModule__(module, exports, $app_require$1) {
                        "use strict";
                        Object.defineProperty(exports, "__esModule", {
                            value: true
                        });
                        exports.default = void 0;
                        var _default = exports.default = {
                            onCreate () {
                                console.log("app created");
                            },
                            onDestroy () {
                                console.log("app destroyed");
                            }
                        };
                    };
                    $app_script$({}, $app_exports$, $app_require$1);
                    $app_exports$.default.style = $app_style$;
                    $app_exports$.default.manifest = __webpack_require__("./src/manifest.json");
                    var $translateStyle$ = function(value) {
                        if ('string' == typeof value) return Object.fromEntries(value.split(';').filter((item)=>Boolean(item && item.trim())).map((item)=>{
                            const matchs = item.match(/([^:]+):(.*)/);
                            if (matchs && matchs.length > 2) return [
                                matchs[1].trim().replace(/-([a-z])/g, (_, match)=>match.toUpperCase()),
                                matchs[2].trim()
                            ];
                            return [];
                        }));
                        return value;
                    };
                    __webpack_require__.g.$translateStyle$ = $translateStyle$;
                })();
            })();
        };
        return createAppHandler();
    })(global, globalThis, window, $app_exports$, $app_evaluate$);
}

//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJmaWxlIjoiYXBwLmpzIiwic291cmNlcyI6WyJ3ZWJwYWNrOi8vZ2VubWluaS1zMS9qc29ufEc6XFx3YXRjaF9hcHBcXC50ZW1wX2dlbm1pbmktczFcXHNyY1xcbWFuaWZlc3QuanNvbiIsIndlYnBhY2s6Ly9nZW5taW5pLXMxL3dlYnBhY2svcnVudGltZS9nbG9iYWwiLCJ3ZWJwYWNrOi8vZ2VubWluaS1zMS93ZWJwYWNrL3J1bnRpbWUvcnNwYWNrX3ZlcnNpb24iLCJ3ZWJwYWNrOi8vZ2VubWluaS1zMS93ZWJwYWNrL3J1bnRpbWUvcnNwYWNrX3VuaXF1ZV9pZCIsIndlYnBhY2s6Ly9nZW5taW5pLXMxL3NyYy9hcHAudXgiXSwic291cmNlc0NvbnRlbnQiOlsibW9kdWxlLmV4cG9ydHMgPSBKU09OLnBhcnNlKCd7XCJwYWNrYWdlXCI6XCJjb20uYXBwbGljYXRpb24ud2F0Y2guZGVtb1wiLFwibmFtZVwiOlwiZ2VubWluaS1zMVwiLFwidmVyc2lvbk5hbWVcIjpcIjEuMC4wXCIsXCJ2ZXJzaW9uQ29kZVwiOjEsXCJtaW5QbGF0Zm9ybVZlcnNpb25cIjoxMDAwLFwiaWNvblwiOlwiL2NvbW1vbi9sb2dvLnBuZ1wiLFwiZGV2aWNlVHlwZUxpc3RcIjpbXCJ3YXRjaFwiXSxcImZlYXR1cmVzXCI6W3tcIm5hbWVcIjpcInN5c3RlbS5yb3V0ZXJcIn1dLFwiY29uZmlnXCI6e1wibG9nTGV2ZWxcIjpcImxvZ1wiLFwiZGVzaWduV2lkdGhcIjpcImRldmljZS13aWR0aFwifSxcInJvdXRlclwiOntcImVudHJ5XCI6XCJwYWdlcy9pbmRleFwiLFwicGFnZXNcIjp7XCJwYWdlcy9pbmRleFwiOntcImNvbXBvbmVudFwiOlwiaW5kZXhcIn0sXCJwYWdlcy9kZXRhaWxcIjp7XCJjb21wb25lbnRcIjpcImRldGFpbFwifX19fScpIiwiX193ZWJwYWNrX3JlcXVpcmVfXy5nID0gKCgpID0+IHtcblx0aWYgKHR5cGVvZiBnbG9iYWxUaGlzID09PSAnb2JqZWN0JykgcmV0dXJuIGdsb2JhbFRoaXM7XG5cdHRyeSB7XG5cdFx0cmV0dXJuIHRoaXMgfHwgbmV3IEZ1bmN0aW9uKCdyZXR1cm4gdGhpcycpKCk7XG5cdH0gY2F0Y2ggKGUpIHtcblx0XHRpZiAodHlwZW9mIHdpbmRvdyA9PT0gJ29iamVjdCcpIHJldHVybiB3aW5kb3c7XG5cdH1cbn0pKCk7IiwiX193ZWJwYWNrX3JlcXVpcmVfXy5ydiA9ICgpID0+IChcIjEuNy4xMlwiKSIsIl9fd2VicGFja19yZXF1aXJlX18ucnVpZCA9IFwiYnVuZGxlcj1yc3BhY2tAMS43LjEyXCI7IiwiPHNjcmlwdD5cbmV4cG9ydCBkZWZhdWx0IHtcbiAgb25DcmVhdGUoKSB7XG4gICAgY29uc29sZS5sb2coXCJhcHAgY3JlYXRlZFwiKVxuICB9LFxuICBvbkRlc3Ryb3koKSB7XG4gICAgY29uc29sZS5sb2coXCJhcHAgZGVzdHJveWVkXCIpXG4gIH1cbn1cbjwvc2NyaXB0PlxuIl0sIm5hbWVzIjpbIm1vZHVsZSIsIkpTT04iLCJfX3dlYnBhY2tfcmVxdWlyZV9fIiwiZ2xvYmFsVGhpcyIsIkZ1bmN0aW9uIiwiZSIsIndpbmRvdyIsIiIsIm9uQ3JlYXRlIiwiY29uc29sZSIsImxvZyIsIm9uRGVzdHJveSJdLCJtYXBwaW5ncyI6Ijs7Ozs7Ozs7Ozs7Ozt3QkFBQUEsT0FBTyxPQUFPLEdBQUdDLEtBQUssS0FBSyxDQUFDOzs7Ozs7Ozs7Ozs7OztvQkNBNUJDLG9CQUFvQixDQUFDLEdBQUcsQUFBQzt3QkFDeEIsSUFBSSxBQUFzQixZQUF0QixPQUFPQyxZQUF5QixPQUFPQTt3QkFDM0MsSUFBSTs0QkFDSCxPQUFPLElBQUksSUFBSSxJQUFJQyxTQUFTO3dCQUM3QixFQUFFLE9BQU9DLEdBQUc7NEJBQ1gsSUFBSSxBQUFrQixZQUFsQixPQUFPQyxRQUFxQixPQUFPQTt3QkFDeEM7b0JBQ0Q7OztvQkNQQUosb0JBQW9CLEVBQUUsR0FBRyxJQUFPOzs7b0JDQWhDQSxvQkFBb0IsSUFBSSxHQUFHOzs7Ozs7Ozs7O3dCQ0MzQkssSUFBQUEsV0FBQUEsUUFBQUEsT0FBQUEsR0FBZTs0QkFDYkM7Z0NBQ0VDLFFBQVFDLEdBQUcsQ0FBQzs0QkFDZDs0QkFDQUM7Z0NBQ0VGLFFBQVFDLEdBQUcsQ0FBQzs0QkFDZDt3QkFDRiJ9