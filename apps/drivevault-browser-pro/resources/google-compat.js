// DriveVault Browser Pro - runs only on accounts.google.com (main frame, before the page's own scripts).
// Qt WebEngine is Chromium without the Chrome product layer: no window.chrome members, and the client hints name only "Chromium".
// Google's sign-in refuses such an engine ("This browser or app may not be secure"), so the page is shown what Chrome shows.
(function () {
    'use strict';
    try {
        var chromeVersion = (/Chrome\/(\d+)/.exec(navigator.userAgent) || [0, '122'])[1];
        var chromeObject = window.chrome && typeof window.chrome === 'object' ? window.chrome : {};
        var define = function (name, value) {
            if (!(name in chromeObject)) Object.defineProperty(chromeObject, name, { value: value, writable: true, configurable: true, enumerable: true });
        };
        define('app', { isInstalled: false, InstallState: { DISABLED: 'disabled', INSTALLED: 'installed', NOT_INSTALLED: 'not_installed' },
                        RunningState: { CANNOT_RUN: 'cannot_run', READY_TO_RUN: 'ready_to_run', RUNNING: 'running' },
                        getDetails: function () { return null; }, getIsInstalled: function () { return false; }, runningState: function () { return 'cannot_run'; } });
        define('csi', function () { return { onloadT: Date.now(), startE: Date.now(), pageT: performance.now(), tran: 15 }; });
        define('loadTimes', function () {
            var now = Date.now() / 1000;
            return { requestTime: now, startLoadTime: now, commitLoadTime: now, finishDocumentLoadTime: now, finishLoadTime: now, firstPaintTime: now,
                     firstPaintAfterLoadTime: 0, navigationType: 'Other', wasFetchedViaSpdy: true, wasNpnNegotiated: true, npnNegotiatedProtocol: 'h2',
                     wasAlternateProtocolAvailable: false, connectionInfo: 'h2' };
        });
        define('runtime', {});
        if (window.chrome !== chromeObject) Object.defineProperty(window, 'chrome', { value: chromeObject, writable: true, configurable: true, enumerable: true });

        var data = navigator.userAgentData;
        if (data && !data.brands.some(function (b) { return b.brand === 'Google Chrome'; })) {
            var withChrome = function (list, full) {
                var out = list.filter(function (b) { return b.brand !== 'Chromium'; }).concat([
                    { brand: 'Chromium', version: full || chromeVersion }, { brand: 'Google Chrome', version: full || chromeVersion }]);
                return out;
            };
            var brands = withChrome(data.brands);
            var proto = Object.getPrototypeOf(data);
            var original = proto.getHighEntropyValues;
            Object.defineProperty(proto, 'brands', { get: function () { return brands; }, configurable: true, enumerable: true });
            proto.getHighEntropyValues = function (hints) {
                return original.call(this, hints).then(function (values) {
                    if (values.brands) values.brands = brands;
                    if (values.fullVersionList) {
                        var chromium = values.fullVersionList.filter(function (b) { return b.brand === 'Chromium'; })[0];
                        values.fullVersionList = withChrome(values.fullVersionList, chromium ? chromium.version : null);
                    }
                    return values;
                });
            };
            proto.toJSON = function () { return { brands: brands, mobile: this.mobile, platform: this.platform }; };
        }
    } catch (e) { /* a page must never fail because of this script */ }
})();
