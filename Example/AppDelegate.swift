//
//  AppDelegate.swift
//  Example
//
//  Created by Robert Payne on 13/07/17.
//

import UIKit
import rethinkdb

@UIApplicationMain
class AppDelegate: UIResponder, UIApplicationDelegate {

    var window: UIWindow?

    private let rethinkdbQueue = DispatchQueue(label: "rethinkdb")


    func application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplicationLaunchOptionsKey: Any]?) -> Bool {
        // Override point for customization after application launch.

        Thread.detachNewThread {
            let appsupport = try! FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: false)
            let dataURL = appsupport.appendingPathComponent("rethinkdb_data")
//            try? FileManager.default.removeItem(at: dataURL)
            try? FileManager.default.createDirectory(at: appsupport, withIntermediateDirectories: true, attributes: nil)
            let args: [String] = [
                "rethinkdb",
                "--bind", "all",
                "-d",
                dataURL.path
            ]

            let argsPtrs = UnsafeMutablePointer<UnsafeMutablePointer<Int8>?>.allocate(capacity: args.count)
            defer {
                for idx in 0..<args.count {
                    argsPtrs.advanced(by: idx).pointee!.deallocate(capacity: Int(args[idx].count))
                }
                argsPtrs.deallocate(capacity: args.count)
            }
            for (idx, arg) in args.enumerated() {
                arg.withCString { ptr in
                    let copy = UnsafeMutablePointer<Int8>.allocate(capacity: strlen(ptr))
                    memcpy(copy, ptr, strlen(ptr))
                    argsPtrs[idx] = copy
                }
            }
            rethinkdb_main(Int32(args.count), argsPtrs)
        }

        return true
    }

    func applicationWillResignActive(_ application: UIApplication) {
        // Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
        // Use this method to pause ongoing tasks, disable timers, and invalidate graphics rendering callbacks. Games should use this method to pause the game.
    }

    func applicationDidEnterBackground(_ application: UIApplication) {
        // Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later.
        // If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
    }

    func applicationWillEnterForeground(_ application: UIApplication) {
        // Called as part of the transition from the background to the active state; here you can undo many of the changes made on entering the background.
    }

    func applicationDidBecomeActive(_ application: UIApplication) {
        // Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
    }

    func applicationWillTerminate(_ application: UIApplication) {
        // Called when the application is about to terminate. Save data if appropriate. See also applicationDidEnterBackground:.
    }


}

